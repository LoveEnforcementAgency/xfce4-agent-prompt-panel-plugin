#include "copilot-ui.h"

static void quit_preview(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    (void) user_data;
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    gchar *executable = NULL;
    gchar *directory = NULL;
    gchar *config = NULL;
    gboolean vertical = FALSE;
    GOptionEntry options[] = {
        { "executable", 0, 0, G_OPTION_ARG_FILENAME, &executable, "Copilot CLI executable", "PATH" },
        { "directory", 0, 0, G_OPTION_ARG_FILENAME, &directory, "Project directory", "PATH" },
        { "config", 0, 0, G_OPTION_ARG_FILENAME, &config, "Optional persistent settings file", "PATH" },
        { "vertical", 0, 0, G_OPTION_ARG_NONE, &vertical, "Preview a vertical panel", NULL },
        { NULL, 0, 0, 0, NULL, NULL, NULL }
    };
    g_autoptr(GOptionContext) context = g_option_context_new("- Copilot panel preview");
    g_option_context_add_main_entries(context, options, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));
    g_autoptr(GError) error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return 1;
    }
    CopilotUi *ui = copilot_ui_new(NULL, config);
    if (executable != NULL || directory != NULL)
        copilot_ui_set_backend(ui, executable, directory);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Copilot Prompt Panel Preview");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_container_add(GTK_CONTAINER(window), copilot_ui_get_widget(ui));
    g_signal_connect(window, "destroy", G_CALLBACK(quit_preview), NULL);
    gtk_widget_show_all(window);
    copilot_ui_set_layout(ui, vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, 32);
    gtk_main();
    g_object_unref(ui);
    g_free(executable);
    g_free(directory);
    g_free(config);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    return 0;
}
