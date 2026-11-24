#define _POSIX_C_SOURCE 200809L
#include <json-glib/json-glib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    g_autofree gchar *cwd = g_get_current_dir();
    gboolean copilot = (argc == 3 || argc == 5) && g_strcmp0(argv[1], "-C") == 0 &&
        g_strcmp0(argv[2], cwd) == 0 &&
        (argc == 3 || g_strcmp0(argv[3], "-i") == 0 ||
         g_strcmp0(argv[3], "--resume") == 0);
    gboolean junie = (argc == 3 || argc == 5 || argc == 6) &&
        g_strcmp0(argv[1], "--project") == 0 && g_strcmp0(argv[2], cwd) == 0 &&
        (argc == 3 || (argc == 5 && g_strcmp0(argv[3], "--prompt") == 0) ||
         (argc == 6 && g_strcmp0(argv[3], "--resume") == 0 &&
          g_strcmp0(argv[4], "--session-id") == 0));
    gboolean claude = argc == 1 ||
        (argc == 2 && g_strcmp0(argv[1], "--resume") != 0) ||
        (argc == 3 && g_strcmp0(argv[1], "--resume") == 0);
    if (!copilot && !junie && !claude)
        return 9;
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_array(builder);
    for (int i = 0; i < argc; i++)
        json_builder_add_string_value(builder, argv[i]);
    json_builder_end_array(builder);
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) generator = json_generator_new();
    json_generator_set_root(generator, root);
    g_autofree gchar *text = json_generator_to_data(generator, NULL);
    if (!g_file_set_contents("interactive-argv.json", text, -1, NULL))
        return 12;
    if (argc == 5 && g_str_equal(argv[4], "keep-terminal-open")) {
        g_autofree gchar *pid = g_strdup_printf("%ld", (long) getpid());
        if (!g_file_set_contents("interactive.pid", pid, -1, NULL))
            return 13;
        for (;;)
            pause();
    }
    return 0;
}
