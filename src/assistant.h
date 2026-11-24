#pragma once

#include <glib.h>

typedef enum {
    ASSISTANT_COPILOT,
    ASSISTANT_JUNIE,
    ASSISTANT_CLAUDE
} Assistant;

const gchar *assistant_name(Assistant assistant);
gchar *assistant_sessions_root(Assistant assistant);
gchar **assistant_build_command(Assistant assistant, const gchar *executable,
                                const gchar *directory, const gchar *prompt,
                                const gchar *session_id);
