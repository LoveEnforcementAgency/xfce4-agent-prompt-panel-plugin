#include "sessions.h"

#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

static gchar *string_member(JsonObject *object, const gchar *key)
{
    JsonNode *node = json_object_get_member(object, key);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE(node) ||
        json_node_get_value_type(node) != G_TYPE_STRING)
        return NULL;
    const gchar *value = json_node_get_string(node);
    return value != NULL ? g_utf8_make_valid(value, -1) : NULL;
}

static gint64 timestamp_member(JsonObject *object, const gchar *key)
{
    JsonNode *node = json_object_get_member(object, key);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE(node))
        return 0;
    GType type = json_node_get_value_type(node);
    gint64 value;
    if (type == G_TYPE_STRING) {
        const gchar *text = json_node_get_string(node);
        if (text == NULL)
            return 0;
        gchar *end = NULL;
        value = g_ascii_strtoll(text, &end, 10);
        if (end == text || *end != '\0')
            return assistant_session_timestamp(text);
    } else if (type == G_TYPE_INT64) {
        value = json_node_get_int(node);
    } else if (type == G_TYPE_DOUBLE) {
        gdouble number = json_node_get_double(node);
        if (!isfinite(number) || number < 0 || number >= (gdouble) G_MAXINT64)
            return 0;
        value = (gint64) number;
    } else {
        return 0;
    }
    return value > G_GINT64_CONSTANT(100000000000) ? value / 1000 : MAX(value, 0);
}

static AssistantSession *read_session(const gchar *root, const gchar *line, gsize length)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, line, (gssize) length, NULL))
        return NULL;
    JsonNode *node = json_parser_get_root(parser);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node))
        return NULL;
    JsonObject *object = json_node_get_object(node);
    g_autofree gchar *id = string_member(object, "sessionId");
    if (id == NULL || *id == '\0' || strchr(id, G_DIR_SEPARATOR) != NULL ||
        g_str_equal(id, ".") || g_str_equal(id, ".."))
        return NULL;
    g_autofree gchar *session_path = g_build_filename(root, id, NULL);
    if (!g_file_test(session_path, G_FILE_TEST_IS_DIR))
        return NULL;
    g_autofree gchar *directory = string_member(object, "projectDir");
    if (directory == NULL || !g_path_is_absolute(directory))
        return NULL;
    AssistantSession *session = g_new0(AssistantSession, 1);
    session->id = g_steal_pointer(&id);
    session->directory = g_steal_pointer(&directory);
    session->name = string_member(object, "taskName");
    if (session->name == NULL || *g_strstrip(session->name) == '\0' ||
        g_str_equal(session->name, "null")) {
        g_free(session->name);
        session->name = g_path_get_basename(session->directory);
    }
    session->updated_at = MAX(timestamp_member(object, "updatedAt"),
                              MAX(timestamp_member(object, "lastPromptAt"),
                                  timestamp_member(object, "createdAt")));
    return session;
}

GPtrArray *junie_sessions_load(const gchar *root, GCancellable *cancel, GError **error)
{
    g_autoptr(GPtrArray) sessions = g_ptr_array_new_with_free_func(assistant_session_free);
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return NULL;
    g_autofree gchar *index_path = g_build_filename(root, "index.jsonl", NULL);
    g_autoptr(GFile) file = g_file_new_for_path(index_path);
    g_autoptr(GError) open_error = NULL;
    g_autoptr(GFileInputStream) input = g_file_read(file, cancel, &open_error);
    if (input == NULL) {
        if (g_error_matches(open_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return g_steal_pointer(&sessions);
        g_propagate_error(error, g_steal_pointer(&open_error));
        return NULL;
    }
    g_autoptr(GDataInputStream) stream = g_data_input_stream_new(G_INPUT_STREAM(input));
    g_data_input_stream_set_newline_type(stream, G_DATA_STREAM_NEWLINE_TYPE_ANY);
    g_autoptr(GHashTable) by_id = g_hash_table_new(g_str_hash, g_str_equal);
    for (;;) {
        if (g_cancellable_set_error_if_cancelled(cancel, error))
            return NULL;
        gsize length = 0;
        g_autoptr(GError) read_error = NULL;
        g_autofree gchar *line = g_data_input_stream_read_line(stream, &length, cancel, &read_error);
        if (line == NULL) {
            if (read_error != NULL) {
                g_propagate_error(error, g_steal_pointer(&read_error));
                return NULL;
            }
            break;
        }
        AssistantSession *session = length <= 65536 ? read_session(root, line, length) : NULL;
        if (session != NULL) {
            AssistantSession *existing = g_hash_table_lookup(by_id, session->id);
            if (existing == NULL) {
                g_hash_table_insert(by_id, session->id, session);
                g_ptr_array_add(sessions, session);
            } else {
                if (session->updated_at >= existing->updated_at) {
                    g_free(existing->name);
                    g_free(existing->directory);
                    existing->name = g_steal_pointer(&session->name);
                    existing->directory = g_steal_pointer(&session->directory);
                    existing->updated_at = session->updated_at;
                }
                assistant_session_free(session);
            }
        }
    }
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return NULL;
    g_hash_table_remove_all(by_id);
    assistant_sessions_sort(sessions);
    return g_steal_pointer(&sessions);
}
