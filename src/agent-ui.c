#include "agent-ui.h"
#include "sessions.h"
#include "terminal.h"

#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>

struct _AgentUi {
    GObject parent_instance;
    XfcePanelPlugin *plugin;
    GtkWidget *panel_box;
    GtkWidget *panel_entry;
    GtkWidget *placeholder_label;
    GtkWidget *settings;
    GtkWidget *directory_chooser;
    GtkWidget *executable_entry;
    GtkWidget *junie_executable_entry;
    GtkWidget *width_spin;
    GtkWidget *settings_error;
    GtkWidget *sessions_menu;
    GCancellable *sessions_cancel;
    gchar *config_path;
    gchar *directory;
    gchar *executable;
    gchar *junie_executable;
    Assistant assistant;
    gint width;
    gint panel_size;
    GtkOrientation orientation;
};

typedef struct {
    GWeakRef ui;
    GtkWidget *menu;
} SessionRequest;

typedef struct {
    Assistant assistant;
    gchar *root;
} SessionQuery;

static void session_query_free(gpointer data)
{
    SessionQuery *query = data;
    g_free(query->root);
    g_free(query);
}

G_DEFINE_TYPE(AgentUi, agent_ui, G_TYPE_OBJECT)

static void show_sessions(AgentUi *ui);
static void popup_sessions(AgentUi *ui);
static void close_sessions(AgentUi *ui);

static void update_tooltip(AgentUi *ui)
{
    const gchar *assistant = assistant_name(ui->assistant);
    g_autofree gchar *text = g_strdup_printf(
        "Enter opens %s, with your prompt if present; Tab switches assistants.\n"
        "Up or Down shows recent %s sessions.\nProject: %s",
        assistant, assistant, ui->directory);
    gtk_widget_set_tooltip_text(ui->panel_entry, text);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(ui->panel_entry), GTK_ENTRY_ICON_SECONDARY, NULL);
}

static void update_placeholder(AgentUi *ui)
{
    const gchar *text = ui->assistant == ASSISTANT_COPILOT ? "Ask Copilot..." : "Ask Junie...";
    gtk_label_set_text(GTK_LABEL(ui->placeholder_label), text);
    if (gtk_entry_get_text_length(GTK_ENTRY(ui->panel_entry)) == 0)
        gtk_widget_show(ui->placeholder_label);
    else
        gtk_widget_hide(ui->placeholder_label);
}

static void panel_entry_changed(GtkEditable *editable, AgentUi *ui)
{
    (void) editable;
    update_placeholder(ui);
}

static gboolean launch_terminal(AgentUi *ui, Assistant assistant, const gchar *directory,
                                const gchar *prompt, const gchar *session_id, GError **error)
{
    const gchar *configured_executable = assistant == ASSISTANT_COPILOT ?
        ui->executable : ui->junie_executable;
    g_autofree gchar *executable = g_find_program_in_path(configured_executable);
    if (executable == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            assistant == ASSISTANT_COPILOT ?
                            "Copilot executable not found. Check the plugin's Properties." :
                            "Junie executable not found. Check the plugin's Properties.");
        return FALSE;
    }
    if (!g_path_is_absolute(directory) ||
        !g_file_test(directory, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "Project folder not found. Check the plugin's Properties.");
        return FALSE;
    }
    g_auto(GStrv) command = assistant_build_command(assistant, executable, directory,
                                                    prompt, session_id);
    return terminal_launch(directory, (const gchar *const *) command, error);
}

static void show_launch_error(AgentUi *ui, const gchar *message)
{
    gtk_widget_set_tooltip_text(ui->panel_entry, message);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(ui->panel_entry), GTK_ENTRY_ICON_SECONDARY,
                                      "dialog-warning");
    gtk_entry_set_icon_activatable(GTK_ENTRY(ui->panel_entry), GTK_ENTRY_ICON_SECONDARY, FALSE);
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(ui->panel_entry), GTK_ENTRY_ICON_SECONDARY, message);
}

static void panel_activate(GtkEntry *entry, AgentUi *ui)
{
    g_autofree gchar *prompt = g_strdup(gtk_entry_get_text(entry));
    g_strstrip(prompt);
    g_autoptr(GError) error = NULL;
    close_sessions(ui);
    if (!launch_terminal(ui, ui->assistant, ui->directory, prompt, NULL, &error)) {
        show_launch_error(ui, error->message);
        return;
    }
    gtk_entry_set_text(entry, "");
    update_tooltip(ui);
}

static gboolean panel_entry_key(GtkWidget *entry, GdkEventKey *event, AgentUi *ui)
{
    (void) entry;
    if ((event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab) &&
        !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) {
        close_sessions(ui);
        ui->assistant = ui->assistant == ASSISTANT_COPILOT ? ASSISTANT_JUNIE : ASSISTANT_COPILOT;
        update_placeholder(ui);
        update_tooltip(ui);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape && ui->sessions_cancel != NULL) {
        g_cancellable_cancel(ui->sessions_cancel);
        if (ui->sessions_menu != NULL)
            gtk_menu_popdown(GTK_MENU(ui->sessions_menu));
        return TRUE;
    }
    if ((event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down ||
         event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up) &&
        !(event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) {
        show_sessions(ui);
        return TRUE;
    }
    return FALSE;
}

static void session_activated(GtkMenuItem *item, AgentUi *ui)
{
    AssistantSession *session = g_object_get_data(G_OBJECT(item), "assistant-session");
    Assistant assistant = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "session-assistant"));
    g_autoptr(GError) error = NULL;
    if (!launch_terminal(ui, assistant, session->directory,
                         NULL, session->id, &error))
        show_launch_error(ui, error->message);
    else
        update_tooltip(ui);
}

static void sessions_closed(GtkWidget *menu, AgentUi *ui)
{
    (void) menu;
    if (ui->sessions_cancel != NULL)
        g_cancellable_cancel(ui->sessions_cancel);
}

static void sessions_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void) source;
    SessionQuery *query = data;
    g_autoptr(GError) error = NULL;
    GPtrArray *sessions = assistant_sessions_load(query->assistant, query->root, cancel, &error);
    if (sessions == NULL) {
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }
    g_task_return_pointer(task, sessions, (GDestroyNotify) g_ptr_array_unref);
}

static gchar *single_line(const gchar *text)
{
    GString *result = g_string_new(NULL);
    for (const gchar *p = text; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar character = g_utf8_get_char(p);
        g_string_append_unichar(result, g_unichar_iscntrl(character) ? ' ' : character);
    }
    return g_string_free(result, FALSE);
}

static void sessions_loaded(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) source;
    SessionRequest *request = data;
    g_autoptr(AgentUi) ui = g_weak_ref_get(&request->ui);
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) sessions = g_task_propagate_pointer(G_TASK(result), &error);
    SessionQuery *query = g_task_get_task_data(G_TASK(result));
    if (ui != NULL && ui->sessions_menu == request->menu &&
        ui->assistant == query->assistant &&
        !g_cancellable_is_cancelled(ui->sessions_cancel)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(request->menu));
        for (GList *item = children; item != NULL; item = item->next)
            gtk_widget_destroy(item->data);
        g_list_free(children);
        if (sessions == NULL || sessions->len == 0) {
            g_autofree gchar *label = g_strdup_printf(
                sessions == NULL ? "Could not read local %s sessions" : "No recent local %s sessions",
                assistant_name(query->assistant));
            GtkWidget *item = gtk_menu_item_new_with_label(label);
            gtk_widget_set_sensitive(item, FALSE);
            gtk_widget_set_tooltip_text(item, error != NULL ? error->message : NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(request->menu), item);
        } else {
            for (guint i = 0; i < sessions->len; i++) {
                AssistantSession *session = g_ptr_array_index(sessions, i);
                const gchar *assistant = assistant_name(query->assistant);
                g_autofree gchar *name = single_line(session->name);
                g_autofree gchar *folder = single_line(session->directory);
                g_autoptr(GDateTime) date = g_date_time_new_from_unix_local(session->updated_at);
                g_autofree gchar *when = date != NULL ? g_date_time_format(date, "%b %d %H:%M") : NULL;
                g_autofree gchar *label = g_strdup_printf("%s: %s\n%s  |  %s", assistant, name, folder,
                                                          when != NULL ? when : "");
                GtkWidget *item = gtk_menu_item_new_with_label(label);
                GtkWidget *text = gtk_bin_get_child(GTK_BIN(item));
                gtk_label_set_max_width_chars(GTK_LABEL(text), 65);
                gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
                gboolean exists = g_file_test(session->directory, G_FILE_TEST_IS_DIR);
                gtk_widget_set_sensitive(item, exists);
                g_autofree gchar *tooltip = g_strdup_printf(
                    "%s\n%s\n%s%s", session->name, session->directory, session->id,
                    exists ? "" : "\nProject folder no longer exists");
                gtk_widget_set_tooltip_text(item, tooltip);
                g_object_set_data_full(G_OBJECT(item), "assistant-session", session,
                                       assistant_session_free);
                g_object_set_data(G_OBJECT(item), "session-assistant",
                                  GINT_TO_POINTER(query->assistant));
                g_signal_connect(item, "activate", G_CALLBACK(session_activated), ui);
                gtk_menu_shell_append(GTK_MENU_SHELL(request->menu), item);
                g_ptr_array_index(sessions, i) = NULL;
            }
        }
        /* Show only once populated: GTK otherwise retains the loading row's
         * screen-constrained height, especially beside a bottom panel. */
        popup_sessions(ui);
        gtk_menu_shell_select_first(GTK_MENU_SHELL(request->menu), TRUE);
    }
    g_weak_ref_clear(&request->ui);
    g_object_unref(request->menu);
    g_free(request);
}

static gboolean sessions_key(GtkWidget *menu, GdkEventKey *event, AgentUi *ui)
{
    if (event->keyval == GDK_KEY_Escape) {
        g_cancellable_cancel(ui->sessions_cancel);
        gtk_menu_popdown(GTK_MENU(menu));
        if (ui->plugin != NULL)
            xfce_panel_plugin_focus_widget(ui->plugin, ui->panel_entry);
        else
            gtk_widget_grab_focus(ui->panel_entry);
        return TRUE;
    }
    return FALSE;
}

static void popup_sessions(AgentUi *ui)
{
    gtk_widget_show_all(ui->sessions_menu);
    g_autoptr(GdkEvent) event = gtk_get_current_event();
    if (event == NULL && gtk_widget_get_realized(ui->panel_entry)) {
        event = gdk_event_new(GDK_KEY_PRESS);
        event->key.window = g_object_ref(gtk_widget_get_window(ui->panel_entry));
        event->key.time = GDK_CURRENT_TIME;
        event->key.keyval = GDK_KEY_Down;
        GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(ui->panel_entry));
        gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
    }
    if (ui->plugin != NULL)
        xfce_panel_plugin_popup_menu(ui->plugin, GTK_MENU(ui->sessions_menu), ui->panel_entry, event);
    else
        gtk_menu_popup_at_widget(GTK_MENU(ui->sessions_menu), ui->panel_entry,
                                 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, event);
}

static void close_sessions(AgentUi *ui)
{
    if (ui->sessions_cancel != NULL)
        g_cancellable_cancel(ui->sessions_cancel);
    if (ui->sessions_menu != NULL) {
        gtk_widget_destroy(ui->sessions_menu);
        g_clear_object(&ui->sessions_menu);
    }
    g_clear_object(&ui->sessions_cancel);
}

static void show_sessions(AgentUi *ui)
{
    close_sessions(ui);
    ui->sessions_cancel = g_cancellable_new();
    ui->sessions_menu = g_object_ref_sink(gtk_menu_new());
    gtk_menu_attach_to_widget(GTK_MENU(ui->sessions_menu), ui->panel_entry, NULL);
    gtk_widget_set_name(ui->sessions_menu, "copilot-sessions");
    GtkWidget *loading = gtk_menu_item_new_with_label("Loading recent sessions...");
    gtk_widget_set_sensitive(loading, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(ui->sessions_menu), loading);
    g_signal_connect(ui->sessions_menu, "deactivate", G_CALLBACK(sessions_closed), ui);
    g_signal_connect(ui->sessions_menu, "key-press-event", G_CALLBACK(sessions_key), ui);
    SessionRequest *request = g_new0(SessionRequest, 1);
    g_weak_ref_init(&request->ui, ui);
    request->menu = g_object_ref(ui->sessions_menu);
    SessionQuery *query = g_new0(SessionQuery, 1);
    query->assistant = ui->assistant;
    query->root = assistant_sessions_root(query->assistant);
    g_autoptr(GTask) task = g_task_new(NULL, ui->sessions_cancel, sessions_loaded, request);
    g_task_set_task_data(task, query, session_query_free);
    g_task_run_in_thread(task, sessions_worker);
}

static gboolean panel_entry_pressed(GtkWidget *entry, GdkEventButton *event, AgentUi *ui)
{
    if (ui->plugin != NULL && event->button == GDK_BUTTON_PRIMARY)
        xfce_panel_plugin_focus_widget(ui->plugin, entry);
    return FALSE;
}

static void load_settings(AgentUi *ui)
{
    if (ui->config_path == NULL)
        return;
    g_autoptr(GKeyFile) file = g_key_file_new();
    if (!g_key_file_load_from_file(file, ui->config_path, G_KEY_FILE_NONE, NULL))
        return;
    g_autofree gchar *directory = g_key_file_get_string(file, "Copilot", "directory", NULL);
    g_autofree gchar *executable = g_key_file_get_string(file, "Copilot", "executable", NULL);
    g_autofree gchar *junie_executable =
        g_key_file_get_string(file, "Copilot", "junie-executable", NULL);
    if (directory != NULL && g_path_is_absolute(directory)) {
        g_free(ui->directory);
        ui->directory = g_strdup(directory);
    }
    if (executable != NULL && *executable != '\0') {
        g_free(ui->executable);
        ui->executable = g_strdup(executable);
    }
    if (junie_executable != NULL && *junie_executable != '\0') {
        g_free(ui->junie_executable);
        ui->junie_executable = g_strdup(junie_executable);
    }
    g_autoptr(GError) error = NULL;
    gint width = g_key_file_get_integer(file, "Copilot", "width", &error);
    if (error == NULL)
        ui->width = CLAMP(width, 120, 600);
}

static gboolean save_settings(AgentUi *ui, GError **error)
{
    if (ui->config_path == NULL)
        return TRUE;
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_key_file_set_string(file, "Copilot", "directory", ui->directory);
    g_key_file_set_string(file, "Copilot", "executable", ui->executable);
    g_key_file_set_string(file, "Copilot", "junie-executable", ui->junie_executable);
    g_key_file_set_integer(file, "Copilot", "width", ui->width);
    gsize length;
    g_autofree gchar *contents = g_key_file_to_data(file, &length, error);
    if (contents == NULL)
        return FALSE;
    g_autofree gchar *parent = g_path_get_dirname(ui->config_path);
    if (g_mkdir_with_parents(parent, 0700) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not create the settings directory.");
        return FALSE;
    }
    return g_file_set_contents_full(ui->config_path, contents, (gssize) length,
                                   G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
                                   0600, error);
}

static void settings_destroyed(GtkWidget *widget, AgentUi *ui)
{
    (void) widget;
    ui->settings = NULL;
    if (ui->plugin != NULL)
        xfce_panel_plugin_unblock_menu(ui->plugin);
}

static gboolean executable_setting_valid(const gchar *executable, const gchar *previous)
{
    if (*executable == '\0')
        return FALSE;
    /* An uninstalled, unchanged assistant must not block other settings. */
    if (g_str_equal(executable, previous))
        return TRUE;
    g_autofree gchar *resolved = g_find_program_in_path(executable);
    return resolved != NULL && g_file_test(resolved, G_FILE_TEST_IS_EXECUTABLE);
}

static void settings_response(GtkDialog *dialog, gint response, AgentUi *ui)
{
    if (response != GTK_RESPONSE_OK) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }
    g_autofree gchar *directory =
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(ui->directory_chooser));
    g_autofree gchar *executable =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(ui->executable_entry)));
    g_autofree gchar *junie_executable =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(ui->junie_executable_entry)));
    g_strstrip(executable);
    g_strstrip(junie_executable);
    if (directory == NULL || !g_file_test(directory, G_FILE_TEST_IS_DIR) ||
        !executable_setting_valid(executable, ui->executable) ||
        !executable_setting_valid(junie_executable, ui->junie_executable)) {
        gtk_label_set_text(GTK_LABEL(ui->settings_error),
                           "Select an existing project folder. Changed CLI paths must point to executables, not shell commands.");
        return;
    }
    gchar *old_directory = ui->directory;
    gchar *old_executable = ui->executable;
    gchar *old_junie_executable = ui->junie_executable;
    gint old_width = ui->width;
    ui->directory = g_steal_pointer(&directory);
    ui->executable = g_steal_pointer(&executable);
    ui->junie_executable = g_steal_pointer(&junie_executable);
    ui->width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->width_spin));
    g_autoptr(GError) error = NULL;
    if (!save_settings(ui, &error)) {
        g_free(ui->directory);
        g_free(ui->executable);
        g_free(ui->junie_executable);
        ui->directory = old_directory;
        ui->executable = old_executable;
        ui->junie_executable = old_junie_executable;
        ui->width = old_width;
        gtk_label_set_text(GTK_LABEL(ui->settings_error), error->message);
        return;
    }
    g_free(old_directory);
    g_free(old_executable);
    g_free(old_junie_executable);
    update_tooltip(ui);
    agent_ui_set_layout(ui, ui->orientation, ui->panel_size);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void agent_ui_dispose(GObject *object)
{
    AgentUi *ui = AGENT_UI(object);
    close_sessions(ui);
    if (ui->settings != NULL)
        gtk_widget_destroy(ui->settings);
    if (ui->panel_box != NULL) {
        gtk_widget_destroy(ui->panel_box);
        g_clear_object(&ui->panel_box);
    }
    ui->plugin = NULL;
    G_OBJECT_CLASS(agent_ui_parent_class)->dispose(object);
}

static void agent_ui_finalize(GObject *object)
{
    AgentUi *ui = AGENT_UI(object);
    g_free(ui->config_path);
    g_free(ui->directory);
    g_free(ui->executable);
    g_free(ui->junie_executable);
    G_OBJECT_CLASS(agent_ui_parent_class)->finalize(object);
}

static void agent_ui_class_init(AgentUiClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = agent_ui_dispose;
    G_OBJECT_CLASS(klass)->finalize = agent_ui_finalize;
}

static void agent_ui_init(AgentUi *ui)
{
    ui->directory = g_strdup(g_get_home_dir());
    ui->executable = g_strdup("copilot");
    ui->junie_executable = g_strdup("junie");
    ui->width = 240;
    ui->panel_size = 32;
    ui->orientation = GTK_ORIENTATION_HORIZONTAL;
    ui->panel_box = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(ui->panel_box, "copilot-panel");
    GtkWidget *entry_overlay = gtk_overlay_new();
    ui->panel_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(ui->panel_entry), 1);
    gtk_widget_set_name(ui->panel_entry, "copilot-panel-entry");
    gtk_container_add(GTK_CONTAINER(entry_overlay), ui->panel_entry);
    ui->placeholder_label = gtk_label_new("Ask Copilot...");
    gtk_widget_set_name(ui->placeholder_label, "copilot-placeholder");
    gtk_label_set_single_line_mode(GTK_LABEL(ui->placeholder_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(ui->placeholder_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(ui->placeholder_label, GTK_ALIGN_START);
    gtk_widget_set_valign(ui->placeholder_label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(ui->placeholder_label, TRUE);
    gtk_widget_set_margin_start(ui->placeholder_label, 8);
    gtk_widget_set_margin_end(ui->placeholder_label, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui->placeholder_label), "dim-label");
    gtk_overlay_add_overlay(GTK_OVERLAY(entry_overlay), ui->placeholder_label);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(entry_overlay), ui->placeholder_label, TRUE);
    gtk_box_pack_start(GTK_BOX(ui->panel_box), entry_overlay, TRUE, TRUE, 0);
    g_signal_connect(ui->panel_entry, "activate", G_CALLBACK(panel_activate), ui);
    g_signal_connect(ui->panel_entry, "key-press-event", G_CALLBACK(panel_entry_key), ui);
    g_signal_connect(ui->panel_entry, "changed", G_CALLBACK(panel_entry_changed), ui);
    g_signal_connect(ui->panel_entry, "button-press-event", G_CALLBACK(panel_entry_pressed), ui);
}

AgentUi *agent_ui_new(XfcePanelPlugin *plugin, const gchar *config_path)
{
    AgentUi *ui = g_object_new(AGENT_TYPE_UI, NULL);
    ui->plugin = plugin;
    ui->config_path = g_strdup(config_path);
    load_settings(ui);
    update_tooltip(ui);
    gtk_widget_show_all(ui->panel_box);
    update_placeholder(ui);
    agent_ui_set_layout(ui, ui->orientation, ui->panel_size);
    return ui;
}

GtkWidget *agent_ui_get_widget(AgentUi *ui)
{
    return ui->panel_box;
}

void agent_ui_set_layout(AgentUi *ui, GtkOrientation orientation, gint size)
{
    ui->orientation = orientation;
    ui->panel_size = size;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(ui->panel_box), orientation);
    gtk_widget_set_size_request(ui->panel_entry,
                               orientation == GTK_ORIENTATION_HORIZONTAL ? ui->width : MAX(size, 1), -1);
}

void agent_ui_set_backend(AgentUi *ui, const gchar *executable, const gchar *directory)
{
    if (executable != NULL) {
        g_free(ui->executable);
        ui->executable = g_strdup(executable);
    }
    if (directory != NULL) {
        g_free(ui->directory);
        ui->directory = g_strdup(directory);
    }
    update_tooltip(ui);
}

void agent_ui_show_settings(AgentUi *ui)
{
    if (ui->settings != NULL) {
        gtk_window_present(GTK_WINDOW(ui->settings));
        return;
    }
    GtkWidget *parent = gtk_widget_get_toplevel(ui->panel_box);
    ui->settings = gtk_dialog_new_with_buttons(
        "Agent Prompt Panel Settings", GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_OK, NULL);
    gtk_window_set_modal(GTK_WINDOW(ui->settings), TRUE);
    g_signal_connect(ui->settings, "response", G_CALLBACK(settings_response), ui);
    g_signal_connect(ui->settings, "destroy", G_CALLBACK(settings_destroyed), ui);
    if (ui->plugin != NULL)
        xfce_panel_plugin_block_menu(ui->plugin);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(ui->settings))),
                       grid, TRUE, TRUE, 0);
    ui->directory_chooser = gtk_file_chooser_button_new("Choose project", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_widget_set_name(ui->directory_chooser, "copilot-directory");
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(ui->directory_chooser), ui->directory);
    ui->executable_entry = gtk_entry_new();
    gtk_widget_set_name(ui->executable_entry, "copilot-executable");
    gtk_entry_set_text(GTK_ENTRY(ui->executable_entry), ui->executable);
    ui->junie_executable_entry = gtk_entry_new();
    gtk_widget_set_name(ui->junie_executable_entry, "junie-executable");
    gtk_entry_set_text(GTK_ENTRY(ui->junie_executable_entry), ui->junie_executable);
    ui->width_spin = gtk_spin_button_new_with_range(120, 600, 10);
    gtk_widget_set_name(ui->width_spin, "copilot-width");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->width_spin), ui->width);
    ui->settings_error = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(ui->settings_error), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(ui->settings_error), 55);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Project folder"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui->directory_chooser, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Copilot executable"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui->executable_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Junie executable"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui->junie_executable_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Panel field width (px)"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui->width_spin, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui->settings_error, 0, 4, 2, 1);
    GtkWidget *note = gtk_label_new("Authenticate with each CLI before launching.\n"
                                   "Terminal sessions use your normal CLI permissions.");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_grid_attach(GTK_GRID(grid), note, 0, 5, 2, 1);
    gtk_widget_show_all(ui->settings);
}
