#include "assistant.h"

const gchar *assistant_name(Assistant assistant)
{
    switch (assistant) {
    case ASSISTANT_JUNIE:
        return "Junie";
    case ASSISTANT_CLAUDE:
        return "Claude";
    case ASSISTANT_COPILOT:
    default:
        return "Copilot";
    }
}

gchar *assistant_sessions_root(Assistant assistant)
{
    if (assistant == ASSISTANT_CLAUDE) {
        const gchar *config_dir = g_getenv("CLAUDE_CONFIG_DIR");
        g_autofree gchar *default_config_dir = g_build_filename(g_get_home_dir(), ".claude", NULL);
        return g_build_filename(config_dir != NULL && *config_dir != '\0'
                                    ? config_dir : default_config_dir,
                                "projects", NULL);
    }

    gboolean junie = assistant == ASSISTANT_JUNIE;
    const gchar *home = g_getenv(junie ? "JUNIE_HOME" : "COPILOT_HOME");
    g_autofree gchar *default_home = g_build_filename(
        g_get_home_dir(), junie ? ".junie" : ".copilot", NULL);
    return g_build_filename(home != NULL && *home != '\0' ? home : default_home,
                            junie ? "sessions" : "session-state", NULL);
}

gchar **assistant_build_command(Assistant assistant, const gchar *executable,
                                const gchar *directory, const gchar *prompt,
                                const gchar *session_id)
{
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(args, g_strdup(executable));
    if (assistant == ASSISTANT_CLAUDE) {
        if (session_id != NULL) {
            g_ptr_array_add(args, g_strdup("--resume"));
            g_ptr_array_add(args, g_strdup(session_id));
        } else if (prompt != NULL && *prompt != '\0') {
            g_ptr_array_add(args, g_strdup(prompt));
        }
    } else {
        g_ptr_array_add(args, g_strdup(assistant == ASSISTANT_JUNIE ? "--project" : "-C"));
        g_ptr_array_add(args, g_strdup(directory));
    }
    if (assistant != ASSISTANT_CLAUDE && session_id != NULL) {
        g_ptr_array_add(args, g_strdup("--resume"));
        if (assistant == ASSISTANT_JUNIE)
            g_ptr_array_add(args, g_strdup("--session-id"));
        g_ptr_array_add(args, g_strdup(session_id));
    } else if (assistant != ASSISTANT_CLAUDE && prompt != NULL && *prompt != '\0') {
        g_ptr_array_add(args, g_strdup(assistant == ASSISTANT_JUNIE ? "--prompt" : "-i"));
        g_ptr_array_add(args, g_strdup(prompt));
    }
    g_ptr_array_add(args, NULL);
    return (gchar **) g_ptr_array_free(args, FALSE);
}
