#include <gtk/gtk.h>
#include <gmodule.h>
#include <libxfce4panel/libxfce4panel.h>
#include <gdk/gdkkeysyms.h>

typedef GtkWidget *(*ConstructPlugin)(const gchar *, gint, const gchar *,
                                     const gchar *, gchar **, GdkScreen *);

typedef struct {
    guint focus_signal;
    guint requests;
    gboolean discovering;
} FocusRequests;

static void provider_signal(GtkWidget *plugin, guint signal, FocusRequests *requests)
{
    (void) plugin;
    if (requests->discovering)
        requests->focus_signal = signal;
    if (signal == requests->focus_signal)
        requests->requests++;
}

static void test_entry_focus(GtkWidget *plugin, GtkWidget *entry,
                            GtkWidget *window)
{
    FocusRequests requests = { .discovering = TRUE };
    g_signal_connect(plugin, "provider-signal", G_CALLBACK(provider_signal), &requests);
    xfce_panel_plugin_focus_widget(XFCE_PANEL_PLUGIN(plugin), entry);
    g_assert_cmpuint(requests.requests, ==, 1);
    requests.discovering = FALSE;
    requests.requests = 0;
    gtk_window_set_focus(GTK_WINDOW(window), NULL);
    GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
    event->button.window = g_object_ref(gtk_widget_get_window(entry));
    event->button.time = GDK_CURRENT_TIME;
    event->button.button = GDK_BUTTON_PRIMARY;
    event->button.x = 8;
    event->button.y = 8;
    GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(entry));
    gdk_event_set_device(event, gdk_seat_get_pointer(seat));
    gtk_widget_event(entry, event);
    gdk_event_free(event);
    g_assert_cmpuint(requests.requests, ==, 1);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(window)) == entry);
    g_signal_handlers_disconnect_by_data(plugin, &requests);
}

static GtkWidget *find_widget(GtkWidget *root, const gchar *name)
{
    if (g_str_equal(gtk_widget_get_name(root), name))
        return root;
    if (!GTK_IS_CONTAINER(root))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *item = children; item != NULL && found == NULL; item = item->next)
        found = find_widget(item->data, name);
    g_list_free(children);
    return found;
}

int main(int argc, char **argv)
{
    if (!gtk_init_check(&argc, &argv))
        return 77;
    g_assert_cmpint(argc, ==, 2);
    GModule *module = g_module_open(argv[1], G_MODULE_BIND_LOCAL);
    if (module == NULL)
        g_error("Cannot load native panel module: %s", g_module_error());
    gpointer symbol = NULL;
    g_assert_true(g_module_symbol(module, "xfce_panel_module_construct", &symbol));
    ConstructPlugin construct = (ConstructPlugin) symbol;
    g_module_make_resident(module);
    GtkWidget *plugin = construct("copilot-test", 987654321, "Copilot Test",
                                   "Native plugin smoke test", NULL, gdk_screen_get_default());
    g_assert_true(XFCE_IS_PANEL_PLUGIN(plugin));
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Copilot Plugin Smoke Test");
    gtk_container_add(GTK_CONTAINER(window), plugin);
    gtk_widget_show_all(window);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    GtkWidget *entry = find_widget(plugin, "copilot-panel-entry");
    GtkWidget *button = find_widget(plugin, "copilot-open");
    g_assert_nonnull(entry);
    g_assert_true(gtk_widget_get_visible(entry));
    g_assert_null(button);
    test_entry_focus(plugin, entry, window);
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *popup = NULL;
    for (GList *item = windows; item != NULL; item = item->next)
        if (g_str_equal(gtk_widget_get_name(item->data), "copilot-chat"))
            popup = item->data;
    g_list_free(windows);
    g_assert_null(popup);
    g_signal_emit_by_name(plugin, "configure-plugin");
    windows = gtk_window_list_toplevels();
    GtkWidget *settings = NULL;
    for (GList *item = windows; item != NULL; item = item->next)
        if (find_widget(item->data, "copilot-executable") != NULL)
            settings = item->data;
    g_list_free(windows);
    g_assert_nonnull(settings);
    g_assert_true(gtk_widget_get_visible(settings));
    g_assert_true(gtk_window_get_transient_for(GTK_WINDOW(settings)) == GTK_WINDOW(window));
    gtk_dialog_response(GTK_DIALOG(settings), GTK_RESPONSE_CANCEL);
    GdkEventKey key = { .type = GDK_KEY_PRESS, .keyval = GDK_KEY_Down };
    gboolean handled = FALSE;
    g_signal_emit_by_name(entry, "key-press-event", &key, &handled);
    g_assert_true(handled);
    GList *menus = gtk_menu_get_for_attach_widget(entry);
    g_assert_nonnull(menus);
    GtkWidget *menu = menus->data;
    g_assert_cmpstr(gtk_widget_get_name(menu), ==, "copilot-sessions");
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    gboolean loaded = FALSE;
    while (!loaded && g_get_monotonic_time() < deadline) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        GList *items = gtk_container_get_children(GTK_CONTAINER(menu));
        if (items != NULL)
            loaded = g_strcmp0(gtk_menu_item_get_label(GTK_MENU_ITEM(items->data)),
                              "Loading recent sessions...") != 0;
        g_list_free(items);
        if (!loaded)
            g_usleep(1000);
    }
    g_assert_true(loaded);
    g_assert_true(gtk_widget_get_visible(menu));
    gtk_menu_popdown(GTK_MENU(menu));
    gtk_widget_destroy(window);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    g_module_close(module);
    return 0;
}
