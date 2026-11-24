#pragma once

#include <libxfce4panel/libxfce4panel.h>

G_BEGIN_DECLS

#define AGENT_TYPE_UI (agent_ui_get_type())
G_DECLARE_FINAL_TYPE(AgentUi, agent_ui, AGENT, UI, GObject)

AgentUi *agent_ui_new(XfcePanelPlugin *plugin, const gchar *config_path);
GtkWidget *agent_ui_get_widget(AgentUi *ui);
void agent_ui_show_settings(AgentUi *ui);
void agent_ui_set_layout(AgentUi *ui, GtkOrientation orientation, gint size);
void agent_ui_set_backend(AgentUi *ui, const gchar *executable, const gchar *directory);

G_END_DECLS
