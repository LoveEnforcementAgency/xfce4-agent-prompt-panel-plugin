#include "sessions.h"

void assistant_session_free(gpointer data)
{
    AssistantSession *session = data;
    if (session == NULL)
        return;
    g_free(session->id);
    g_free(session->name);
    g_free(session->directory);
    g_free(session);
}

gint64 assistant_session_timestamp(const gchar *text)
{
    if (text == NULL)
        return 0;
    g_autoptr(GDateTime) date = g_date_time_new_from_iso8601(text, NULL);
    return date != NULL ? g_date_time_to_unix(date) : 0;
}

static gint compare_sessions(gconstpointer left, gconstpointer right)
{
    const AssistantSession *a = *(AssistantSession * const *) left;
    const AssistantSession *b = *(AssistantSession * const *) right;
    if (a->updated_at != b->updated_at)
        return a->updated_at > b->updated_at ? -1 : 1;
    return g_strcmp0(a->id, b->id);
}

void assistant_sessions_sort(GPtrArray *sessions)
{
    g_ptr_array_sort(sessions, compare_sessions);
    if (sessions->len > 20)
        g_ptr_array_set_size(sessions, 20);
}

GPtrArray *assistant_sessions_load(Assistant assistant, const gchar *root,
                                   GCancellable *cancel, GError **error)
{
    switch (assistant) {
    case ASSISTANT_JUNIE:
        return junie_sessions_load(root, cancel, error);
    case ASSISTANT_CLAUDE:
        return claude_sessions_load(root, cancel, error);
    case ASSISTANT_COPILOT:
    default:
        return copilot_sessions_load(root, cancel, error);
    }
}
