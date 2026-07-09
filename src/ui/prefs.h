#pragma once
#include <adwaita.h>

typedef void (*PrefsChangedFn)(gpointer data);

void prefs_dialog_show(GtkWidget *parent, PrefsChangedFn on_changed, gpointer data);
