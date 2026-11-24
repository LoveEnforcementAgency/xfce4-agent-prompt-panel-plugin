#include "assistant.h"

static void test_commands(void)
{
    const gchar *directory = "/project with spaces";
    const gchar *prompt = "quote '\"; $(touch not-a-command)\n--allow-all";
    for (Assistant assistant = ASSISTANT_COPILOT; assistant <= ASSISTANT_JUNIE; assistant++) {
        const gchar *project_option = assistant == ASSISTANT_JUNIE ? "--project" : "-C";
        const gchar *prompt_option = assistant == ASSISTANT_JUNIE ? "--prompt" : "-i";
        g_auto(GStrv) fresh = assistant_build_command(assistant, "/fake cli", directory, "", NULL);
        const gchar *expected_fresh[] = { "/fake cli", project_option, directory, NULL };
        g_assert_cmpstrv(fresh, expected_fresh);
        g_auto(GStrv) prompted = assistant_build_command(
            assistant, "/fake cli", directory, prompt, NULL);
        const gchar *expected_prompt[] = {
            "/fake cli", project_option, directory, prompt_option, prompt, NULL
        };
        g_assert_cmpstrv(prompted, expected_prompt);
        g_auto(GStrv) resumed = assistant_build_command(
            assistant, "/fake cli", directory, NULL, "session-id");
        const gchar *expected_copilot[] = {
            "/fake cli", "-C", directory, "--resume", "session-id", NULL
        };
        const gchar *expected_junie[] = {
            "/fake cli", "--project", directory, "--resume", "--session-id", "session-id", NULL
        };
        if (assistant == ASSISTANT_JUNIE)
            g_assert_cmpstrv(resumed, expected_junie);
        else
            g_assert_cmpstrv(resumed, expected_copilot);
    }

    g_auto(GStrv) fresh = assistant_build_command(
        ASSISTANT_CLAUDE, "claude", directory, "", NULL);
    const gchar *expected_fresh[] = { "claude", NULL };
    g_assert_cmpstrv(fresh, expected_fresh);
    g_auto(GStrv) prompted = assistant_build_command(
        ASSISTANT_CLAUDE, "claude", directory, prompt, NULL);
    const gchar *expected_prompt[] = { "claude", prompt, NULL };
    g_assert_cmpstrv(prompted, expected_prompt);
    g_auto(GStrv) resumed = assistant_build_command(
        ASSISTANT_CLAUDE, "claude", directory, NULL, "session-id");
    const gchar *expected_resumed[] = { "claude", "--resume", "session-id", NULL };
    g_assert_cmpstrv(resumed, expected_resumed);
}

static void test_names_and_roots(void)
{
    const gchar *old_config_dir = g_getenv("CLAUDE_CONFIG_DIR");
    g_autofree gchar *saved_config_dir = g_strdup(old_config_dir);
    g_assert_cmpstr(assistant_name(ASSISTANT_CLAUDE), ==, "Claude");
    g_setenv("CLAUDE_CONFIG_DIR", "/custom/claude-config", TRUE);
    g_autofree gchar *configured_root = assistant_sessions_root(ASSISTANT_CLAUDE);
    g_assert_cmpstr(configured_root, ==, "/custom/claude-config/projects");
    g_unsetenv("CLAUDE_CONFIG_DIR");
    g_autofree gchar *default_root = assistant_sessions_root(ASSISTANT_CLAUDE);
    g_autofree gchar *expected_root = g_build_filename(g_get_home_dir(), ".claude", "projects", NULL);
    g_assert_cmpstr(default_root, ==, expected_root);
    if (saved_config_dir != NULL)
        g_setenv("CLAUDE_CONFIG_DIR", saved_config_dir, TRUE);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/assistant/interactive-command-contracts", test_commands);
    g_test_add_func("/assistant/names-and-session-roots", test_names_and_roots);
    return g_test_run();
}
