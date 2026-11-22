#pragma once

#include <libxfce4panel/libxfce4panel.h>

G_BEGIN_DECLS

#define COPILOT_TYPE_UI (copilot_ui_get_type())
G_DECLARE_FINAL_TYPE(CopilotUi, copilot_ui, COPILOT, UI, GObject)

CopilotUi *copilot_ui_new(XfcePanelPlugin *plugin, const gchar *config_path);
GtkWidget *copilot_ui_get_widget(CopilotUi *ui);
void copilot_ui_show_settings(CopilotUi *ui);
void copilot_ui_set_layout(CopilotUi *ui, GtkOrientation orientation, gint size);
void copilot_ui_set_backend(CopilotUi *ui, const gchar *executable, const gchar *directory);

G_END_DECLS
