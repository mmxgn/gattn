#pragma once
#include "../session.h"

typedef void (*SidebarNewFn)(GtkWidget *split, gpointer data);

GtkWidget *sidebar_new(SessionList *sessions, SidebarNewFn on_new, gpointer on_new_data);
void       sidebar_add_session(GtkWidget *split, Session *s);
void       sidebar_remove_session(GtkWidget *split, int id);
void       sidebar_show_diff(Session *s, GtkWidget *parent_widget);
void       sidebar_rename_session(Session *s, GtkWidget *parent_widget);
