#pragma once
#include "../session.h"

typedef void (*SidebarNewFn)(GtkWidget *split, gpointer data);

GtkWidget *sidebar_new(SessionList *sessions, SidebarNewFn on_new, gpointer on_new_data);
void       sidebar_add_session(GtkWidget *split, Session *s);
