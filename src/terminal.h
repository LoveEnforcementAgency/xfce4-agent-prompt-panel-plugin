#pragma once

#include <gio/gio.h>

gboolean terminal_launch(const gchar *directory, const gchar *const *command, GError **error);
