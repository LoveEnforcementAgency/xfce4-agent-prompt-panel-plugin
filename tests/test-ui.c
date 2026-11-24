#define _POSIX_C_SOURCE 200809L
#include "agent-ui.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <gdk/gdkkeysyms.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static const gchar *fake_cli;
static gchar *directory;
static gchar *junie_cli;
static gchar *claude_cli;
static gchar *session_home;

static void send_key(GtkWidget *widget, guint keyval);
static void send_key_direct(GtkWidget *widget, guint keyval);

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

static void wait_terminal_args_at(const gchar *working_directory,
                                  const gchar *const *expected, guint expected_count)
{
    g_autofree gchar *path = g_build_filename(working_directory, "interactive-argv.json", NULL);
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
    g_assert_cmpuint(json_array_get_length(args), ==, expected_count);
    for (guint i = 0; i < expected_count; i++)
        g_assert_cmpstr(json_array_get_string_element(args, i), ==, expected[i]);
    g_assert_null(find_window("copilot-chat"));
    g_assert_cmpint(g_remove(path), ==, 0);
}

static void wait_terminal_args(const gchar *const *expected, guint expected_count)
{
    wait_terminal_args_at(directory, expected, expected_count);
}

static void wait_terminal_command(const gchar *option, const gchar *value)
{
    const gchar *expected[] = { fake_cli, "-C", directory, option, value };
    wait_terminal_args(expected, option != NULL ? G_N_ELEMENTS(expected) : 3);
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
    g_autoptr(AgentUi) ui = agent_ui_new(NULL, NULL);
    agent_ui_set_backend(ui, fake_cli, directory);
    GtkWidget *root = agent_ui_get_widget(ui);
    GtkWidget *entry = find_widget(root, "copilot-panel-entry");
    GtkWidget *placeholder = find_widget(root, "copilot-placeholder");
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    g_assert_cmpuint(g_list_length(children), ==, 1);
    g_assert_true(GTK_IS_OVERLAY(children->data));
    g_assert_true(children->data == gtk_widget_get_parent(entry));
    g_assert_cmpstr(gtk_entry_get_placeholder_text(GTK_ENTRY(entry)), ==, NULL);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Copilot...");
    gtk_entry_set_text(GTK_ENTRY(entry), "Junie prompt");
    send_key_direct(entry, GDK_KEY_Tab);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Junie...");
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "Junie prompt");
    send_key_direct(entry, GDK_KEY_Tab);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Claude...");
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "Junie prompt");
    send_key_direct(entry, GDK_KEY_Tab);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Copilot...");
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "Junie prompt");
    g_list_free(children);
    g_assert_null(find_widget(root, "copilot-open"));
    send_key_direct(entry, GDK_KEY_Tab);
    g_signal_emit_by_name(entry, "activate");
    const gchar *junie_args[] = { junie_cli, "--project", directory, "--prompt", "Junie prompt" };
    wait_terminal_args(junie_args, G_N_ELEMENTS(junie_args));
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "");
    g_signal_emit_by_name(entry, "activate");
    const gchar *junie_fresh_args[] = { junie_cli, "--project", directory };
    wait_terminal_args(junie_fresh_args, G_N_ELEMENTS(junie_fresh_args));
    send_key_direct(entry, GDK_KEY_Tab);
    send_key_direct(entry, GDK_KEY_Tab);
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

    agent_ui_set_backend(ui, "/nonexistent/copilot", directory);
    gtk_entry_set_text(GTK_ENTRY(entry), "keep this panel prompt");
    g_signal_emit_by_name(entry, "activate");
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "keep this panel prompt");
    g_autofree gchar *tooltip = gtk_widget_get_tooltip_text(entry);
    g_assert_nonnull(strstr(tooltip, "Copilot executable not found"));
    g_assert_null(find_window("copilot-chat"));
    agent_ui_set_backend(ui, fake_cli, directory);
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_launch("keep this panel prompt");
    g_assert_null(gtk_entry_get_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY));

    agent_ui_set_layout(ui, GTK_ORIENTATION_VERTICAL, 32);
    g_assert_true(gtk_widget_get_visible(entry));
    agent_ui_set_layout(ui, GTK_ORIENTATION_HORIZONTAL, 32);
    g_assert_true(gtk_widget_get_visible(entry));
}

static void test_claude_launches(void)
{
    g_autofree gchar *terminal = g_find_program_in_path("exo-open");
    if (terminal == NULL) {
        g_test_skip("Xfce preferred-application launcher is not installed");
        return;
    }
    g_autoptr(AgentUi) ui = agent_ui_new(NULL, NULL);
    agent_ui_set_backend(ui, fake_cli, directory);
    GtkWidget *entry = find_widget(agent_ui_get_widget(ui), "copilot-panel-entry");
    gtk_entry_set_text(GTK_ENTRY(entry), "Claude prompt");
    send_key_direct(entry, GDK_KEY_Tab);
    send_key_direct(entry, GDK_KEY_Tab);
    GtkWidget *placeholder = find_widget(agent_ui_get_widget(ui), "copilot-placeholder");
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Claude...");
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "Claude prompt");
    g_signal_emit_by_name(entry, "activate");
    const gchar *prompted[] = { claude_cli, "Claude prompt" };
    wait_terminal_args(prompted, G_N_ELEMENTS(prompted));
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "");
    g_signal_emit_by_name(entry, "activate");
    const gchar *fresh[] = { claude_cli };
    wait_terminal_args(fresh, G_N_ELEMENTS(fresh));
}

static void test_placeholder_focus(void)
{
    g_autoptr(AgentUi) ui = agent_ui_new(NULL, NULL);
    GtkWidget *root = agent_ui_get_widget(ui);
    GtkWidget *entry = find_widget(root, "copilot-panel-entry");
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(window), root);
    gtk_widget_show_all(window);
    gtk_window_set_focus(GTK_WINDOW(window), entry);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);

    g_assert_true(gtk_window_get_focus(GTK_WINDOW(window)) == entry);
    GtkWidget *placeholder = find_widget(root, "copilot-placeholder");
    g_assert_nonnull(placeholder);
    g_assert_true(gtk_widget_get_visible(placeholder));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Copilot...");

    gtk_entry_set_text(GTK_ENTRY(entry), "draft");
    g_assert_false(gtk_widget_get_visible(placeholder));
    send_key_direct(entry, GDK_KEY_Tab);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Junie...");
    send_key_direct(entry, GDK_KEY_Tab);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(placeholder)), ==, "Ask Claude...");
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "draft");
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    g_assert_true(gtk_widget_get_visible(placeholder));
    gtk_widget_destroy(window);
}

static void test_settings(void)
{
    g_autofree gchar *path = g_build_filename(directory, "settings.rc", NULL);
    g_autoptr(AgentUi) ui = agent_ui_new(NULL, path);
    agent_ui_set_backend(ui, fake_cli, directory);
    agent_ui_show_settings(ui);
    GtkWidget *executable = find_window("copilot-executable");
    g_assert_nonnull(executable);
    g_assert_nonnull(find_window("junie-executable"));
    GtkWidget *claude_executable = find_window("claude-executable");
    g_assert_nonnull(claude_executable);
    GtkWidget *dialog = gtk_widget_get_toplevel(executable);
    g_assert_null(find_window("copilot-chat"));
    gtk_entry_set_text(GTK_ENTRY(executable), "not-an-executable --allow-all");
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_assert_true(gtk_widget_get_visible(dialog));
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    gtk_entry_set_text(GTK_ENTRY(claude_executable), "not-an-executable --allow-all");
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_assert_true(gtk_widget_get_visible(dialog));
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    gtk_entry_set_text(GTK_ENTRY(executable), fake_cli);
    gtk_entry_set_text(GTK_ENTRY(claude_executable), fake_cli);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(find_widget(dialog, "copilot-width")), 310);
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < deadline) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
    g_autoptr(GKeyFile) saved = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(saved, path, G_KEY_FILE_NONE, NULL));
    g_autofree gchar *saved_claude = g_key_file_get_string(
        saved, "Copilot", "claude-executable", NULL);
    g_assert_cmpstr(saved_claude, ==, fake_cli);
    g_clear_object(&ui);
    ui = agent_ui_new(NULL, path);
    GtkWidget *entry = find_widget(agent_ui_get_widget(ui), "copilot-panel-entry");
    gint width;
    gtk_widget_get_size_request(entry, &width, NULL);
    g_assert_cmpint(width, ==, 310);
    gtk_entry_set_text(GTK_ENTRY(entry), "settings-restored");
    g_signal_emit_by_name(entry, "activate");
    wait_terminal_launch("settings-restored");
    send_key_direct(entry, GDK_KEY_Tab);
    gtk_entry_set_text(GTK_ENTRY(entry), "settings-junie");
    g_signal_emit_by_name(entry, "activate");
    const gchar *junie_args[] = { junie_cli, "--project", directory, "--prompt", "settings-junie" };
    wait_terminal_args(junie_args, G_N_ELEMENTS(junie_args));
    g_assert_cmpint(g_remove(path), ==, 0);
}

static void test_settings_without_optional_assistants(void)
{
    g_autofree gchar *path = g_build_filename(directory, "optional-settings.rc", NULL);
    g_autofree gchar *config = g_strdup_printf(
        "[Copilot]\ndirectory=%s\nexecutable=/nonexistent/copilot\n"
        "junie-executable=/nonexistent/junie\nclaude-executable=/nonexistent/claude\n"
        "width=240\n", directory);
    g_assert_true(g_file_set_contents(path, config, -1, NULL));
    g_autoptr(AgentUi) ui = agent_ui_new(NULL, path);
    agent_ui_show_settings(ui);
    GtkWidget *dialog = gtk_widget_get_toplevel(find_window("copilot-executable"));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(find_widget(dialog, "copilot-width")), 330);
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < deadline) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_assert_null(find_window("copilot-width"));
    g_autoptr(GKeyFile) saved = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(saved, path, G_KEY_FILE_NONE, NULL));
    g_assert_cmpint(g_key_file_get_integer(saved, "Copilot", "width", NULL), ==, 330);
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

static void send_key_direct(GtkWidget *widget, guint keyval)
{
    GdkEventKey event = { .type = GDK_KEY_PRESS, .keyval = keyval };
    gboolean handled = FALSE;
    g_signal_emit_by_name(widget, "key-press-event", &event, &handled);
    g_assert_true(handled);
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
    g_autoptr(AgentUi) ui = agent_ui_new(NULL, NULL);
    agent_ui_set_backend(ui, fake_cli, directory);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_container_add(GTK_CONTAINER(window), agent_ui_get_widget(ui));
    gtk_widget_show_all(window);
    GdkMonitor *monitor = gdk_display_get_monitor_at_window(
        gtk_widget_get_display(window), gtk_widget_get_window(window));
    GdkRectangle area;
    gdk_monitor_get_workarea(monitor, &area);
    gtk_window_move(GTK_WINDOW(window), area.x + 100, area.y + area.height - 40);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    GtkWidget *entry = find_widget(agent_ui_get_widget(ui), "copilot-panel-entry");
    send_key(entry, GDK_KEY_Up);
    GtkWidget *menu = entry_menu(entry);
    wait_sessions(menu, 1);
    GList *items = gtk_container_get_children(GTK_CONTAINER(menu));
    g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(items->data)), ==, "No recent local Copilot sessions");
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
    agent_ui_set_backend(ui, fake_cli, session_home);
    send_key(entry, GDK_KEY_KP_Up);
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

    const gchar *junie_id = "session-260101-120000-test";
    g_autofree gchar *junie_root = g_build_filename(session_home, "sessions", NULL);
    g_assert_cmpint(g_mkdir(junie_root, 0700), ==, 0);
    g_autofree gchar *junie_session = g_build_filename(junie_root, junie_id, NULL);
    g_assert_cmpint(g_mkdir(junie_session, 0700), ==, 0);
    g_autofree gchar *index_path = g_build_filename(junie_root, "index.jsonl", NULL);
    g_autofree gchar *index = g_strdup_printf(
        "{\"sessionId\":\"%s\",\"createdAt\":1795520000000,"
        "\"updatedAt\":1795521000000,\"lastPromptAt\":1795521000000,"
        "\"projectDir\":\"%s\",\"taskName\":\"Junie task\"}\n",
        junie_id, directory);
    g_assert_true(g_file_set_contents(index_path, index, -1, NULL));
    const gchar *claude_id = "00000000-0000-4000-8000-000000000003";
    g_autofree gchar *claude_project = g_build_filename(session_home, "claude-project", NULL);
    g_assert_cmpint(g_mkdir(claude_project, 0700), ==, 0);
    g_autofree gchar *claude_projects = g_build_filename(session_home, "projects", NULL);
    g_assert_cmpint(g_mkdir(claude_projects, 0700), ==, 0);
    g_autofree gchar *claude_project_data = g_build_filename(claude_projects, "-claude-project", NULL);
    g_assert_cmpint(g_mkdir(claude_project_data, 0700), ==, 0);
    g_autofree gchar *claude_filename = g_strconcat(claude_id, ".jsonl", NULL);
    g_autofree gchar *claude_history_path = g_build_filename(
        claude_project_data, claude_filename, NULL);
    g_autofree gchar *claude_history = g_strdup_printf(
        "{\"type\":\"user\",\"sessionId\":\"%s\",\"cwd\":\"%s\","
        "\"timestamp\":\"2030-01-01T10:00:00Z\",\"message\":{\"content\":\"private prompt\"}}\n"
        "{\"type\":\"summary\",\"sessionId\":\"%s\",\"cwd\":\"%s\","
        "\"summary\":\"Claude task\",\"timestamp\":\"2030-01-01T10:05:00Z\"}\n",
        claude_id, claude_project, claude_id, claude_project);
    g_assert_true(g_file_set_contents(claude_history_path, claude_history, -1, NULL));
    send_key(entry, GDK_KEY_Up);
    menu = entry_menu(entry);
    wait_sessions(menu, 2);
    items = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *item = items; item != NULL; item = item->next) {
        const gchar *label = gtk_menu_item_get_label(GTK_MENU_ITEM(item->data));
        g_assert_true(g_str_has_prefix(label, "Copilot: Session "));
        g_assert_null(strstr(label, "Junie"));
    }
    g_list_free(items);
    send_key(menu, GDK_KEY_Escape);

    gtk_entry_set_text(GTK_ENTRY(entry), "draft stays intact");
    send_key_direct(entry, GDK_KEY_Down);
    g_autoptr(GtkWidget) stale_menu = g_object_ref(entry_menu(entry));
    send_key_direct(entry, GDK_KEY_Tab);
    g_assert_null(gtk_menu_get_for_attach_widget(entry));
    send_key_direct(entry, GDK_KEY_Down);
    menu = entry_menu(entry);
    wait_sessions(menu, 1);
    g_assert_false(gtk_widget_get_visible(stale_menu));
    items = gtk_container_get_children(GTK_CONTAINER(menu));
    g_assert_true(g_str_has_prefix(gtk_menu_item_get_label(GTK_MENU_ITEM(items->data)),
                                   "Junie: Junie task"));
    g_list_free(items);
    send_key(menu, GDK_KEY_Return);
    const gchar *junie_resume_args[] = {
        junie_cli, "--project", directory, "--resume", "--session-id", junie_id
    };
    wait_terminal_args(junie_resume_args, G_N_ELEMENTS(junie_resume_args));
    g_assert_false(gtk_widget_get_visible(menu));
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "draft stays intact");

    send_key_direct(entry, GDK_KEY_ISO_Left_Tab);
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "draft stays intact");
    send_key_direct(entry, GDK_KEY_Down);
    menu = entry_menu(entry);
    wait_sessions(menu, 1);
    items = gtk_container_get_children(GTK_CONTAINER(menu));
    g_assert_true(g_str_has_prefix(gtk_menu_item_get_label(GTK_MENU_ITEM(items->data)),
                                   "Claude: Claude task"));
    g_list_free(items);
    send_key(menu, GDK_KEY_Return);
    const gchar *claude_resume_args[] = { claude_cli, "--resume", claude_id };
    wait_terminal_args_at(claude_project, claude_resume_args, G_N_ELEMENTS(claude_resume_args));
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "draft stays intact");

    send_key_direct(entry, GDK_KEY_Tab);
    send_key_direct(entry, GDK_KEY_Down);
    menu = entry_menu(entry);
    wait_sessions(menu, 2);
    send_key(menu, GDK_KEY_Escape);
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
    g_assert_cmpint(g_remove(index_path), ==, 0);
    g_assert_cmpint(g_rmdir(junie_session), ==, 0);
    g_assert_cmpint(g_rmdir(junie_root), ==, 0);
    g_assert_cmpint(g_remove(claude_history_path), ==, 0);
    g_assert_cmpint(g_rmdir(claude_project_data), ==, 0);
    g_assert_cmpint(g_rmdir(claude_projects), ==, 0);
    g_assert_cmpint(g_rmdir(claude_project), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    session_home = g_dir_make_tmp("copilot-picker-test-XXXXXX", NULL);
    g_assert_nonnull(session_home);
    g_setenv("COPILOT_HOME", session_home, TRUE);
    g_setenv("JUNIE_HOME", session_home, TRUE);
    g_setenv("CLAUDE_CONFIG_DIR", session_home, TRUE);
    if (!gtk_init_check(&argc, &argv)) {
        g_assert_cmpint(g_rmdir(session_home), ==, 0);
        g_free(session_home);
        return 77;
    }
    g_assert_cmpint(argc, ==, 2);
    fake_cli = argv[1];
    directory = g_dir_make_tmp("agent-ui-test-XXXXXX", NULL);
    g_assert_nonnull(directory);
    junie_cli = g_build_filename(directory, "junie", NULL);
    g_assert_cmpint(symlink(fake_cli, junie_cli), ==, 0);
    claude_cli = g_build_filename(directory, "claude", NULL);
    g_assert_cmpint(symlink(fake_cli, claude_cli), ==, 0);
    g_autofree gchar *path = g_strdup_printf("%s:%s", directory,
                                              g_getenv("PATH") != NULL ? g_getenv("PATH") : "");
    g_setenv("PATH", path, TRUE);
    g_test_add_func("/ui/entry-only-terminal-launch-and-recovery", test_terminal);
    g_test_add_func("/ui/claude-prompt-and-fresh-launch", test_claude_launches);
    g_test_add_func("/ui/placeholder-visible-while-focused", test_placeholder_focus);
    g_test_add_func("/ui/settings-validation-and-persistence", test_settings);
    g_test_add_func("/ui/settings-with-uninstalled-assistants", test_settings_without_optional_assistants);
    g_test_add_func("/ui/recent-session-menu-keyboard-resume-and-cancel", test_recent_sessions);
    int result = g_test_run();
    g_assert_cmpint(g_remove(junie_cli), ==, 0);
    g_assert_cmpint(g_remove(claude_cli), ==, 0);
    g_free(junie_cli);
    g_free(claude_cli);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(directory);
    g_assert_cmpint(g_rmdir(session_home), ==, 0);
    g_free(session_home);
    return result;
}
