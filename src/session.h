#pragma once
#include <gtk/gtk.h>

typedef enum {
    SESSION_IDLE,
    SESSION_WORKING,
    SESSION_NEEDS_INPUT,
    SESSION_DONE,
} SessionState;

typedef struct {
    int        id;
    char       name[64];
    SessionState state;
    GtkWidget *terminal; /* VteTerminal, wired in phase 2 */
    GtkWidget *dot;      /* sidebar state indicator, set by sidebar_add_session */
    int        pid;
    guint      poll_id;       /* hook file poll timer */
    guint      idle_timer_id; /* heuristic silence timer */
} Session;

typedef struct {
    Session items[32];
    int     count;
} SessionList;

void     session_list_init(SessionList *list);
Session *session_create(SessionList *list, const char *name);
void     session_destroy(SessionList *list, int id);
void     session_set_state(Session *s, SessionState state);
void     session_spawn(Session *s, const char *cmd); /* NULL = $SHELL */
