#include "sessions.h"

#include <glib/gstdio.h>
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

static gint64 normalize_timestamp(gdouble value)
{
    if (!isfinite(value) || value < 0 || value >= (gdouble) G_MAXINT64)
        return 0;
    gint64 timestamp = (gint64) value;
    return timestamp > G_GINT64_CONSTANT(100000000000) ? timestamp / 1000 : timestamp;
}

static gint64 timestamp_member(JsonObject *object, const gchar *key)
{
    JsonNode *node = json_object_get_member(object, key);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE(node))
        return 0;
    GType type = json_node_get_value_type(node);
    if (type == G_TYPE_STRING) {
        const gchar *text = json_node_get_string(node);
        if (text == NULL)
            return 0;
        gchar *end = NULL;
        gint64 value = g_ascii_strtoll(text, &end, 10);
        if (end != text && *end == '\0')
            return normalize_timestamp((gdouble) value);
        return assistant_session_timestamp(text);
    }
    if (type == G_TYPE_INT64 || type == G_TYPE_INT)
        return normalize_timestamp((gdouble) json_node_get_int(node));
    if (type == G_TYPE_DOUBLE)
        return normalize_timestamp(json_node_get_double(node));
    return 0;
}

static gchar *clean_name(const gchar *text)
{
    if (text == NULL)
        return NULL;
    g_autofree gchar *name = g_utf8_make_valid(text, -1);
    for (gchar *character = name; *character != '\0'; character++) {
        if (*character == '\n' || *character == '\r' || *character == '\t')
            *character = ' ';
    }
    g_strstrip(name);
    if (*name == '\0')
        return NULL;
    if (g_utf8_strlen(name, -1) > 100) {
        g_autofree gchar *short_name = g_utf8_substring(name, 0, 100);
        return g_strconcat(short_name, "…", NULL);
    }
    return g_steal_pointer(&name);
}

static void update_session(AssistantSession **session_pointer, const gchar *id,
                           const gchar *line, gsize length)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, line, (gssize) length, NULL))
        return;
    JsonNode *node = json_parser_get_root(parser);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node))
        return;
    JsonObject *object = json_node_get_object(node);
    g_autofree gchar *record_id = string_member(object, "sessionId");
    if (g_strcmp0(record_id, id) != 0)
        return;

    AssistantSession *session = *session_pointer;
    g_autofree gchar *directory = string_member(object, "cwd");
    if (session == NULL && (directory == NULL || !g_path_is_absolute(directory)))
        return;
    if (session == NULL) {
        session = g_new0(AssistantSession, 1);
        session->id = g_strdup(id);
        session->directory = g_steal_pointer(&directory);
        session->name = g_path_get_basename(session->directory);
        *session_pointer = session;
    } else if (directory != NULL && g_path_is_absolute(directory)) {
        g_free(session->directory);
        session->directory = g_steal_pointer(&directory);
        if (session->name == NULL)
            session->name = g_path_get_basename(session->directory);
    }

    session->updated_at = MAX(session->updated_at, timestamp_member(object, "timestamp"));
    g_autofree gchar *raw_summary = string_member(object, "summary");
    g_autofree gchar *summary = clean_name(raw_summary);
    if (summary != NULL) {
        g_free(session->name);
        session->name = g_steal_pointer(&summary);
    }
}

static AssistantSession *read_session(const gchar *path, const gchar *id,
                                      GCancellable *cancel, GError **error)
{
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GError) open_error = NULL;
    g_autoptr(GFileInputStream) input = g_file_read(file, cancel, &open_error);
    if (input == NULL) {
        if (g_error_matches(open_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return NULL;
        g_propagate_error(error, g_steal_pointer(&open_error));
        return NULL;
    }

    g_autoptr(GDataInputStream) stream = g_data_input_stream_new(G_INPUT_STREAM(input));
    g_data_input_stream_set_newline_type(stream, G_DATA_STREAM_NEWLINE_TYPE_ANY);
    AssistantSession *session = NULL;
    for (;;) {
        if (g_cancellable_set_error_if_cancelled(cancel, error)) {
            assistant_session_free(session);
            return NULL;
        }
        gsize length = 0;
        g_autoptr(GError) read_error = NULL;
        g_autofree gchar *line = g_data_input_stream_read_line(
            stream, &length, cancel, &read_error);
        if (line == NULL) {
            if (read_error != NULL) {
                assistant_session_free(session);
                g_propagate_error(error, g_steal_pointer(&read_error));
                return NULL;
            }
            break;
        }
        if (length <= 65536)
            update_session(&session, id, line, length);
    }

    if (session != NULL) {
        GStatBuf stat_buffer;
        if (g_stat(path, &stat_buffer) == 0)
            session->updated_at = MAX(session->updated_at, (gint64) stat_buffer.st_mtime);
    }
    return session;
}

static gboolean is_missing_error(const GError *error)
{
    return g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
           g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

GPtrArray *claude_sessions_load(const gchar *root, GCancellable *cancel, GError **error)
{
    g_autoptr(GPtrArray) sessions = g_ptr_array_new_with_free_func(assistant_session_free);
    g_autoptr(GHashTable) by_id = g_hash_table_new(g_str_hash, g_str_equal);
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return NULL;
    g_autoptr(GError) open_error = NULL;
    g_autoptr(GDir) projects = g_dir_open(root, 0, &open_error);
    if (projects == NULL) {
        if (is_missing_error(open_error))
            return g_steal_pointer(&sessions);
        g_propagate_error(error, g_steal_pointer(&open_error));
        return NULL;
    }

    const gchar *project_name;
    while ((project_name = g_dir_read_name(projects)) != NULL) {
        if (g_cancellable_set_error_if_cancelled(cancel, error))
            return NULL;
        g_autofree gchar *project_path = g_build_filename(root, project_name, NULL);
        if (!g_file_test(project_path, G_FILE_TEST_IS_DIR))
            continue;
        g_autoptr(GError) project_error = NULL;
        g_autoptr(GDir) project = g_dir_open(project_path, 0, &project_error);
        if (project == NULL) {
            if (is_missing_error(project_error))
                continue;
            g_propagate_error(error, g_steal_pointer(&project_error));
            return NULL;
        }

        const gchar *filename;
        while ((filename = g_dir_read_name(project)) != NULL) {
            if (g_cancellable_set_error_if_cancelled(cancel, error))
                return NULL;
            if (!g_str_has_suffix(filename, ".jsonl"))
                continue;
            g_autofree gchar *id = g_strndup(filename, strlen(filename) - strlen(".jsonl"));
            if (!g_uuid_string_is_valid(id))
                continue;
            g_autofree gchar *path = g_build_filename(project_path, filename, NULL);
            if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
                continue;
            g_autoptr(GError) read_error = NULL;
            AssistantSession *session = read_session(path, id, cancel, &read_error);
            if (read_error != NULL) {
                g_propagate_error(error, g_steal_pointer(&read_error));
                return NULL;
            }
            if (session == NULL)
                continue;
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