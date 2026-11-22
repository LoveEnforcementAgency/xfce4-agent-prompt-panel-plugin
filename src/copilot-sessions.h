#pragma once

#include <gio/gio.h>

typedef struct {
    gchar *id;
    gchar *name;
    gchar *directory;
    gint64 updated_at;
} CopilotSession;

void copilot_session_free(gpointer data);
GPtrArray *copilot_sessions_load(const gchar *root, GCancellable *cancel, GError **error);
