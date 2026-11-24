#include "agent-ui.h"

static void plugin_construct(XfcePanelPlugin *plugin);

XFCE_PANEL_PLUGIN_REGISTER(plugin_construct)

static gboolean size_changed(XfcePanelPlugin *plugin, gint size, AgentUi *ui)
{
    agent_ui_set_layout(ui, xfce_panel_plugin_get_orientation(plugin), size);
    return TRUE;
}

static void mode_changed(XfcePanelPlugin *plugin, XfcePanelPluginMode mode, AgentUi *ui)
{
    (void) mode;
    agent_ui_set_layout(ui, xfce_panel_plugin_get_orientation(plugin),
                          xfce_panel_plugin_get_size(plugin));
}

static void configure_plugin(XfcePanelPlugin *plugin, AgentUi *ui)
{
    (void) plugin;
    agent_ui_show_settings(ui);
}

static void free_plugin(XfcePanelPlugin *plugin, AgentUi *ui)
{
    (void) plugin;
    g_object_unref(ui);
}

static void plugin_construct(XfcePanelPlugin *plugin)
{
    g_autofree gchar *path = xfce_panel_plugin_save_location(plugin, TRUE);
    AgentUi *ui = agent_ui_new(plugin, path);
    gtk_container_add(GTK_CONTAINER(plugin), agent_ui_get_widget(ui));
    xfce_panel_plugin_set_small(plugin, FALSE);
    xfce_panel_plugin_set_expand(plugin, FALSE);
    xfce_panel_plugin_add_action_widget(plugin, agent_ui_get_widget(ui));
    xfce_panel_plugin_menu_show_configure(plugin);
    g_signal_connect(plugin, "size-changed", G_CALLBACK(size_changed), ui);
    g_signal_connect(plugin, "mode-changed", G_CALLBACK(mode_changed), ui);
    g_signal_connect(plugin, "configure-plugin", G_CALLBACK(configure_plugin), ui);
    g_signal_connect(plugin, "free-data", G_CALLBACK(free_plugin), ui);
    size_changed(plugin, xfce_panel_plugin_get_size(plugin), ui);
}
