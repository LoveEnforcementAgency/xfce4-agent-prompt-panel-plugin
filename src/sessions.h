#pragma once

#include <gio/gio.h>
#include "assistant.h"

typedef struct {
    gchar *id;
    gchar *name;
    gchar *directory;
    gint64 updated_at;
} AssistantSession;

void assistant_session_free(gpointer data);
gint64 assistant_session_timestamp(const gchar *text);
void assistant_sessions_sort(GPtrArray *sessions);
GPtrArray *assistant_sessions_load(Assistant assistant, const gchar *root,
                                   GCancellable *cancel, GError **error);
GPtrArray *copilot_sessions_load(const gchar *root, GCancellable *cancel, GError **error);
GPtrArray *junie_sessions_load(const gchar *root, GCancellable *cancel, GError **error);
GPtrArray *claude_sessions_load(const gchar *root, GCancellable *cancel, GError **error);
