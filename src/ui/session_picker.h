#pragma once
#include <gtk/gtk.h>

typedef void (*SessionPickedFn)(const char *cmd, const char *dir, gpointer data);
void session_picker_show(GtkWidget *parent_win, SessionPickedFn picked, gpointer data,
                         const char *initial_dir);
