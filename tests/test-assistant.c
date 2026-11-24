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
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/assistant/interactive-command-contracts", test_commands);
    return g_test_run();
}
