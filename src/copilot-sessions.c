#include "copilot-sessions.h"

#include <glib/gstdio.h>
#include <yaml.h>

void copilot_session_free(gpointer data)
{
    CopilotSession *session = data;
    if (session == NULL)
        return;
    g_free(session->id);
    g_free(session->name);
    g_free(session->directory);
    g_free(session);
}

static gchar *scalar_member(yaml_document_t *document, yaml_node_t *root, const gchar *key)
{
    for (yaml_node_pair_t *pair = root->data.mapping.pairs.start;
         pair < root->data.mapping.pairs.top; pair++) {
        yaml_node_t *name = yaml_document_get_node(document, pair->key);
        yaml_node_t *value = yaml_document_get_node(document, pair->value);
        if (name->type == YAML_SCALAR_NODE && value->type == YAML_SCALAR_NODE &&
            g_strcmp0((gchar *) name->data.scalar.value, key) == 0)
            return g_utf8_make_valid((gchar *) value->data.scalar.value,
                                     (gssize) value->data.scalar.length);
    }
    return NULL;
}

static gint64 timestamp(const gchar *text)
{
    if (text == NULL)
        return 0;
    g_autoptr(GDateTime) date = g_date_time_new_from_iso8601(text, NULL);
    return date != NULL ? g_date_time_to_unix(date) : 0;
}

static CopilotSession *read_session(const gchar *root, const gchar *id, GCancellable *cancel)
{
    g_autofree gchar *metadata = g_build_filename(root, id, "workspace.yaml", NULL);
    g_autofree gchar *history = g_build_filename(root, id, "events.jsonl", NULL);
    GStatBuf metadata_stat, history_stat;
    if (g_stat(metadata, &metadata_stat) != 0 || metadata_stat.st_size > 65536 ||
        !S_ISREG(metadata_stat.st_mode) ||
        g_stat(history, &history_stat) != 0 || !S_ISREG(history_stat.st_mode) ||
        history_stat.st_size == 0)
        return NULL;
    g_autoptr(GFile) file = g_file_new_for_path(metadata);
    g_autofree gchar *contents = NULL;
    gsize length;
    if (!g_file_load_contents(file, cancel, &contents, &length, NULL, NULL) || length > 65536)
        return NULL;
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return NULL;
    yaml_parser_set_input_string(&parser, (const unsigned char *) contents, length);
    yaml_document_t document;
    gboolean parsed = yaml_parser_load(&parser, &document);
    yaml_parser_delete(&parser);
    if (!parsed)
        return NULL;
    CopilotSession *session = NULL;
    yaml_node_t *mapping = yaml_document_get_root_node(&document);
    if (mapping != NULL && mapping->type == YAML_MAPPING_NODE) {
        g_autofree gchar *saved_id = scalar_member(&document, mapping, "id");
        g_autofree gchar *cwd = scalar_member(&document, mapping, "cwd");
        if (g_strcmp0(saved_id, id) == 0 && cwd != NULL && g_path_is_absolute(cwd)) {
            session = g_new0(CopilotSession, 1);
            session->id = g_strdup(id);
            session->directory = g_steal_pointer(&cwd);
            session->name = scalar_member(&document, mapping, "name");
            if (session->name == NULL || *g_strstrip(session->name) == '\0' ||
                g_str_equal(session->name, "null") || g_str_equal(session->name, "~")) {
                g_free(session->name);
                session->name = g_path_get_basename(session->directory);
            }
            g_autofree gchar *updated = scalar_member(&document, mapping, "updated_at");
            g_autofree gchar *created = scalar_member(&document, mapping, "created_at");
            session->updated_at = MAX(timestamp(updated), timestamp(created));
            /* Active CLI sessions do not always rewrite workspace.yaml. */
            session->updated_at = MAX(session->updated_at, (gint64) history_stat.st_mtime);
        }
    }
    yaml_document_delete(&document);
    return session;
}

static gint compare_sessions(gconstpointer left, gconstpointer right)
{
    const CopilotSession *a = *(CopilotSession * const *) left;
    const CopilotSession *b = *(CopilotSession * const *) right;
    if (a->updated_at != b->updated_at)
        return a->updated_at > b->updated_at ? -1 : 1;
    return g_strcmp0(a->id, b->id);
}

GPtrArray *copilot_sessions_load(const gchar *root, GCancellable *cancel, GError **error)
{
    g_autoptr(GPtrArray) sessions = g_ptr_array_new_with_free_func(copilot_session_free);
    g_autoptr(GError) open_error = NULL;
    g_autoptr(GDir) directory = g_dir_open(root, 0, &open_error);
    if (directory == NULL) {
        if (g_error_matches(open_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            return g_steal_pointer(&sessions);
        g_propagate_error(error, g_steal_pointer(&open_error));
        return NULL;
    }
    const gchar *name;
    while ((name = g_dir_read_name(directory)) != NULL) {
        if (g_cancellable_set_error_if_cancelled(cancel, error))
            return NULL;
        if (!g_uuid_string_is_valid(name))
            continue;
        CopilotSession *session = read_session(root, name, cancel);
        if (session != NULL)
            g_ptr_array_add(sessions, session);
    }
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return NULL;
    g_ptr_array_sort(sessions, compare_sessions);
    if (sessions->len > 20)
        g_ptr_array_set_size(sessions, 20);
    return g_steal_pointer(&sessions);
}
