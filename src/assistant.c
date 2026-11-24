#include "assistant.h"

const gchar *assistant_name(Assistant assistant)
{
    return assistant == ASSISTANT_JUNIE ? "Junie" : "Copilot";
}

gchar *assistant_sessions_root(Assistant assistant)
{
    const gchar *home = g_getenv(assistant == ASSISTANT_JUNIE ? "JUNIE_HOME" : "COPILOT_HOME");
    g_autofree gchar *default_home = g_build_filename(
        g_get_home_dir(), assistant == ASSISTANT_JUNIE ? ".junie" : ".copilot", NULL);
    return g_build_filename(home != NULL && *home != '\0' ? home : default_home,
                            assistant == ASSISTANT_JUNIE ? "sessions" : "session-state", NULL);
}

gchar **assistant_build_command(Assistant assistant, const gchar *executable,
                                const gchar *directory, const gchar *prompt,
                                const gchar *session_id)
{
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(args, g_strdup(executable));
    g_ptr_array_add(args, g_strdup(assistant == ASSISTANT_JUNIE ? "--project" : "-C"));
    g_ptr_array_add(args, g_strdup(directory));
    if (session_id != NULL) {
        g_ptr_array_add(args, g_strdup("--resume"));
        if (assistant == ASSISTANT_JUNIE)
            g_ptr_array_add(args, g_strdup("--session-id"));
        g_ptr_array_add(args, g_strdup(session_id));
    } else if (prompt != NULL && *prompt != '\0') {
        g_ptr_array_add(args, g_strdup(assistant == ASSISTANT_JUNIE ? "--prompt" : "-i"));
        g_ptr_array_add(args, g_strdup(prompt));
    }
    g_ptr_array_add(args, NULL);
    return (gchar **) g_ptr_array_free(args, FALSE);
}
