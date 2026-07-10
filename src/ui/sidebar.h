#pragma once
#include "../session.h"

typedef void (*SidebarNewFn)(GtkWidget *split, gpointer data);
typedef void (*SidebarShellHereFn)(GtkWidget *split, const char *cwd, gpointer data);
void sidebar_set_shell_here(GtkWidget *split, SidebarShellHereFn fn, gpointer data);

GtkWidget *sidebar_new(SessionList *sessions, SidebarNewFn on_new, gpointer on_new_data);
void       sidebar_add_session(GtkWidget *split, Session *s);
void       sidebar_remove_session(GtkWidget *split, int id);
void       sidebar_show_diff(Session *s, GtkWidget *parent_widget);
void       sidebar_rename_session(Session *s, GtkWidget *parent_widget);
void       sidebar_toggle_search(GtkWidget *split);
/* 0 = wide (all visible); 1 = hide inline action buttons; 2 = also hide cwd label. */
void sidebar_set_compact_level(GtkWidget *split, int level);
