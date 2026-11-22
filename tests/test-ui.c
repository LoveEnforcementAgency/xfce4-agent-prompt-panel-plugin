#define _POSIX_C_SOURCE 200809L
#include "copilot-ui.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <gdk/gdkkeysyms.h>
#include <signal.h>
#include <stdlib.h>

static const gchar *fake_cli;
static gchar *directory;
static gchar *session_home;

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

static GtkWidget *find_window(const gchar *name)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *item = windows; item != NULL && found == NULL; item = item->next)
        found = find_widget(item->data, name);
    g_list_free(windows);
    return found;
}

static void wait_terminal_command(const gchar *option, const gchar *value)
{
    g_autofree gchar *path = g_build_filename(directory, "interactive-argv.json", NULL);
    g_autoptr(JsonParser) parser = json_parser_new();
    gboolean received = FALSE;
    gint64 deadline = g_get_monotonic_time() + 8 * G_TIME_SPAN_SECOND;
    while (!received && g_get_monotonic_time() < deadline) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        received = json_parser_load_from_file(parser, path, NULL);
        if (!received)
            g_usleep(1000);
    }
    g_assert_true(received);
    JsonArray *args = json_node_get_array(json_parser_get_root(parser));
    g_assert_cmpuint(json_array_get_length(args), ==, option != NULL ? 5 : 3);
    g_assert_cmpstr(json_array_get_string_element(args, 0), ==, fake_cli);
    g_assert_cmpstr(json_array_get_string_element(args, 1), ==, "-C");
    g_assert_cmpstr(json_array_get_string_element(args, 2), ==, directory);
    if (option != NULL) {
        g_assert_cmpstr(json_array_get_string_element(args, 3), ==, option);
        g_assert_cmpstr(json_array_get_string_element(args, 4), ==, value);
    }
    g_assert_null(find_window("copilot-chat"));
    g_assert_cmpint(g_remove(path), ==, 0);
}

static void wait_terminal_launch(const gchar *prompt)
{
    wait_terminal_command("-i", prompt);
}

static void test_terminal(void)
{
    g_autofree gchar *terminal = g_find_program_in_path("exo-open");
    if (terminal == NULL) {
        g_test_skip("Xfce preferred-application launcher is not installed");
        return;
    }
    g_autoptr(CopilotUi) ui = copilot_ui_new(NULL, NULL);
    copilot_ui_set_backend(ui, fake_cli, directory);
    GtkWidget *root = copilot_ui_get_widget(ui);
    GtkWidget *entry = find_widget(root, "copilot-panel-entry");
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    g_assert_cmpuint(g_list_length(children), ==, 1);
    g_assert_true(children->data == entry);
    g_list_free(children);
    g_assert_null(find_widget(root, "copilot-open"));
    gtk_entry_set_text(GTK_ENTRY(entry), "  ");
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_command(NULL, NULL);
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "");
    g_assert_null(gtk_menu_get_for_attach_widget(entry));
    const gchar *draft = "tailscale status '\"; $(touch terminal-should-not-exist)\n--allow-all";
    gtk_entry_set_text(GTK_ENTRY(entry), draft);
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_launch(draft);
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "");
    g_autofree gchar *marker = g_build_filename(directory, "terminal-should-not-exist", NULL);
    g_assert_false(g_file_test(marker, G_FILE_TEST_EXISTS));

    gtk_entry_set_text(GTK_ENTRY(entry), "keep-terminal-open");
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_launch("keep-terminal-open");
    g_autofree gchar *pid_path = g_build_filename(directory, "interactive.pid", NULL);
    g_autofree gchar *pid_text = NULL;
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (!g_file_get_contents(pid_path, &pid_text, NULL, NULL) &&
           g_get_monotonic_time() < deadline)
        g_usleep(1000);
    g_assert_nonnull(pid_text);
    pid_t pid = (pid_t) strtol(pid_text, NULL, 10);
    g_assert_cmpint(pid, >, 0);
    gtk_entry_set_text(GTK_ENTRY(entry), "second terminal");
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_launch("second terminal");
    g_assert_cmpint(kill(pid, 0), ==, 0);
    g_assert_cmpint(kill(pid, SIGTERM), ==, 0);
    g_assert_cmpint(g_remove(pid_path), ==, 0);

    copilot_ui_set_backend(ui, "/nonexistent/copilot", directory);
    gtk_entry_set_text(GTK_ENTRY(entry), "keep this panel prompt");
    g_signal_emit_by_name(entry, "activate");
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "keep this panel prompt");
    g_autofree gchar *tooltip = gtk_widget_get_tooltip_text(entry);
    g_assert_nonnull(strstr(tooltip, "Copilot executable not found"));
    g_assert_null(find_window("copilot-chat"));
    copilot_ui_set_backend(ui, fake_cli, directory);
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_launch("keep this panel prompt");
    g_assert_null(gtk_entry_get_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY));

    copilot_ui_set_layout(ui, GTK_ORIENTATION_VERTICAL, 32);
    g_assert_true(gtk_widget_get_visible(entry));
    copilot_ui_set_layout(ui, GTK_ORIENTATION_HORIZONTAL, 32);
    g_assert_true(gtk_widget_get_visible(entry));
}

static void test_settings(void)
{
    g_autofree gchar *path = g_build_filename(directory, "settings.rc", NULL);
    g_autoptr(CopilotUi) ui = copilot_ui_new(NULL, path);
    copilot_ui_set_backend(ui, fake_cli, directory);
    copilot_ui_show_settings(ui);
    GtkWidget *executable = find_window("copilot-executable");
    g_assert_nonnull(executable);
    GtkWidget *dialog = gtk_widget_get_toplevel(executable);
    g_assert_null(find_window("copilot-chat"));
    gtk_entry_set_text(GTK_ENTRY(executable), "not-an-executable --allow-all");
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_assert_true(gtk_widget_get_visible(dialog));
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    gtk_entry_set_text(GTK_ENTRY(executable), fake_cli);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(find_widget(dialog, "copilot-width")), 310);
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < deadline) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
    g_clear_object(&ui);
    ui = copilot_ui_new(NULL, path);
    GtkWidget *entry = find_widget(copilot_ui_get_widget(ui), "copilot-panel-entry");
    gint width;
    gtk_widget_get_size_request(entry, &width, NULL);
    g_assert_cmpint(width, ==, 310);
    gtk_entry_set_text(GTK_ENTRY(entry), "settings-restored");
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_launch("settings-restored");
    g_assert_cmpint(g_remove(path), ==, 0);
}

static void send_key(GtkWidget *widget, guint keyval)
{
    GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
    if (gtk_widget_get_realized(widget))
        event->key.window = g_object_ref(gtk_widget_get_window(widget));
    event->key.time = GDK_CURRENT_TIME;
    event->key.keyval = keyval;
    GdkKeymapKey *keys = NULL;
    gint count = 0;
    GdkKeymap *keymap = gdk_keymap_get_for_display(gtk_widget_get_display(widget));
    g_assert_true(gdk_keymap_get_entries_for_keyval(keymap, keyval, &keys, &count));
    event->key.hardware_keycode = keys[0].keycode;
    event->key.group = keys[0].group;
    g_free(keys);
    GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(widget));
    gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
    if (gtk_widget_get_realized(widget))
        gtk_widget_event(widget, event);
    else {
        gboolean handled = FALSE;
        g_signal_emit_by_name(widget, "key-press-event", event, &handled);
    }
    gdk_event_free(event);
}

static GtkWidget *entry_menu(GtkWidget *entry)
{
    GList *menus = gtk_menu_get_for_attach_widget(entry);
    g_assert_nonnull(menus);
    return menus->data;
}

static void wait_sessions(GtkWidget *menu, guint count)
{
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    for (;;) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        GList *items = gtk_container_get_children(GTK_CONTAINER(menu));
        gboolean ready = g_list_length(items) == count &&
            (count != 1 || !g_str_equal(gtk_menu_item_get_label(GTK_MENU_ITEM(items->data)),
                                        "Loading recent sessions..."));
        g_list_free(items);
        if (ready)
            return;
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_recent_sessions(void)
{
    g_autoptr(CopilotUi) ui = copilot_ui_new(NULL, NULL);
    copilot_ui_set_backend(ui, fake_cli, directory);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_container_add(GTK_CONTAINER(window), copilot_ui_get_widget(ui));
    gtk_widget_show_all(window);
    GdkMonitor *monitor = gdk_display_get_monitor_at_window(
        gtk_widget_get_display(window), gtk_widget_get_window(window));
    GdkRectangle area;
    gdk_monitor_get_workarea(monitor, &area);
    gtk_window_move(GTK_WINDOW(window), area.x + 100, area.y + area.height - 40);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    GtkWidget *entry = find_widget(copilot_ui_get_widget(ui), "copilot-panel-entry");
    send_key(entry, GDK_KEY_Down);
    GtkWidget *menu = entry_menu(entry);
    wait_sessions(menu, 1);
    GList *items = gtk_container_get_children(GTK_CONTAINER(menu));
    g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(items->data)), ==, "No recent local sessions");
    g_list_free(items);
    send_key(menu, GDK_KEY_Escape);
    g_assert_false(gtk_widget_get_visible(menu));
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(window)) == entry);

    g_autofree gchar *root = g_build_filename(session_home, "session-state", NULL);
    g_assert_cmpint(g_mkdir(root, 0700), ==, 0);
    const gchar *ids[] = {
        "00000000-0000-4000-8000-000000000001",
        "00000000-0000-4000-8000-000000000002"
    };
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++) {
        g_autofree gchar *path = g_build_filename(root, ids[i], NULL);
        g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
        g_autofree gchar *metadata_path = g_build_filename(path, "workspace.yaml", NULL);
        g_autofree gchar *history = g_build_filename(path, "events.jsonl", NULL);
        g_autofree gchar *metadata = g_strdup_printf(
            "id: %s\ncwd: %s\nname: Session %u\nupdated_at: 2030-01-0%uT12:00:00Z\n",
            ids[i], directory, i + 1, i + 1);
        g_assert_true(g_file_set_contents(metadata_path, metadata, -1, NULL));
        g_assert_true(g_file_set_contents(history, "{}\n", -1, NULL));
    }
    copilot_ui_set_backend(ui, fake_cli, session_home);
    send_key(entry, GDK_KEY_Down);
    menu = entry_menu(entry);
    wait_sessions(menu, 2);
    items = gtk_container_get_children(GTK_CONTAINER(menu));
    gint minimum_height = 0;
    for (GList *item = items; item != NULL; item = item->next) {
        gint height;
        gtk_widget_get_preferred_height(item->data, &height, NULL);
        minimum_height += height;
    }
    gint64 resize_deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (gtk_widget_get_allocated_height(menu) < minimum_height &&
           g_get_monotonic_time() < resize_deadline) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_assert_cmpint(gtk_widget_get_allocated_height(menu), >=, minimum_height);
    g_assert_nonnull(strstr(gtk_menu_item_get_label(GTK_MENU_ITEM(items->data)), "Session 2"));
    g_assert_true(gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(menu)) == items->data);
    send_key(menu, GDK_KEY_Down);
    g_assert_true(gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(menu)) == items->next->data);
    send_key(menu, GDK_KEY_Up);
    g_assert_true(gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(menu)) == items->data);
    send_key(menu, GDK_KEY_Down);
    send_key(menu, GDK_KEY_Return);
    g_list_free(items);
    wait_terminal_command("--resume", ids[0]);
    g_assert_false(gtk_widget_get_visible(menu));
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "");

    gtk_entry_set_text(GTK_ENTRY(entry), "draft stays intact");
    send_key(entry, GDK_KEY_Down);
    menu = entry_menu(entry);
    send_key(menu, GDK_KEY_Escape);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    g_assert_false(gtk_widget_get_visible(menu));
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "draft stays intact");
    send_key(entry, GDK_KEY_Down);
    gtk_widget_destroy(window);
    g_clear_object(&ui);
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++) {
        g_autofree gchar *path = g_build_filename(root, ids[i], NULL);
        g_autofree gchar *metadata_path = g_build_filename(path, "workspace.yaml", NULL);
        g_autofree gchar *history = g_build_filename(path, "events.jsonl", NULL);
        g_assert_cmpint(g_remove(metadata_path), ==, 0);
        g_assert_cmpint(g_remove(history), ==, 0);
        g_assert_cmpint(g_rmdir(path), ==, 0);
    }
    g_assert_cmpint(g_rmdir(root), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    session_home = g_dir_make_tmp("copilot-picker-test-XXXXXX", NULL);
    g_assert_nonnull(session_home);
    g_setenv("COPILOT_HOME", session_home, TRUE);
    if (!gtk_init_check(&argc, &argv)) {
        g_assert_cmpint(g_rmdir(session_home), ==, 0);
        g_free(session_home);
        return 77;
    }
    g_assert_cmpint(argc, ==, 2);
    fake_cli = argv[1];
    directory = g_dir_make_tmp("copilot-ui-test-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_test_add_func("/ui/entry-only-terminal-launch-and-recovery", test_terminal);
    g_test_add_func("/ui/settings-validation-and-persistence", test_settings);
    g_test_add_func("/ui/recent-session-menu-keyboard-resume-and-cancel", test_recent_sessions);
    int result = g_test_run();
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(directory);
    g_assert_cmpint(g_rmdir(session_home), ==, 0);
    g_free(session_home);
    return result;
}
