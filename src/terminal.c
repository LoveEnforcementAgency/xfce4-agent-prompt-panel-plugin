#include "terminal.h"

#include <libxfce4util/libxfce4util.h>

static gboolean prefers_terminator(void)
{
    g_autofree gchar *path = xfce_resource_lookup(XFCE_RESOURCE_CONFIG, "xfce4/helpers.rc");
    if (path == NULL)
        return FALSE;
    XfceRc *config = xfce_rc_simple_open(path, TRUE);
    if (config == NULL)
        return FALSE;
    gboolean preferred = g_strcmp0(xfce_rc_read_entry(config, "TerminalEmulator", NULL),
                                  "terminator") == 0;
    xfce_rc_close(config);
    return preferred;
}

gboolean terminal_launch(const gchar *directory, const gchar *const *command, GError **error)
{
    if (prefers_terminator()) {
        /* Independent windows must not forward to an existing D-Bus server. */
        g_autoptr(GPtrArray) args = g_ptr_array_new();
        g_ptr_array_add(args, "terminator");
        g_ptr_array_add(args, "--no-dbus");
        g_ptr_array_add(args, "--working-directory");
        g_ptr_array_add(args, (gpointer) directory);
        g_ptr_array_add(args, "-x");
        for (guint i = 0; command[i] != NULL; i++)
            g_ptr_array_add(args, (gpointer) command[i]);
        g_ptr_array_add(args, NULL);
        return g_spawn_async(directory, (gchar **) args->pdata, NULL,
                             G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, error);
    }
    g_autoptr(GString) quoted_command = g_string_new(NULL);
    for (guint i = 0; command[i] != NULL; i++) {
        g_autofree gchar *quoted = g_shell_quote(command[i]);
        if (i > 0)
            g_string_append_c(quoted_command, ' ');
        g_string_append(quoted_command, quoted);
    }
    gchar *args[] = {
        "exo-open", "--working-directory", (gchar *) directory,
        "--launch", "TerminalEmulator", quoted_command->str, NULL
    };
    return g_spawn_async(directory, args, NULL, G_SPAWN_SEARCH_PATH,
                         NULL, NULL, NULL, error);
}
