#pragma once
#include <gtk/gtk.h>

typedef enum {
    SESSION_IDLE,
    SESSION_WORKING,
    SESSION_NEEDS_INPUT,
    SESSION_DONE,
} SessionState;

typedef struct Session Session;
typedef void (*SessionStateChangedFn)(Session *s, gpointer data);
typedef void (*SessionChildSpawnedFn)(int child_pid, int parent_id, gpointer data);

struct Session {
    int                   id;
    int                   parent_id; /* 0 = top-level */
    char                  name[64];
    char                  cmd[128];
    char                  cwd[256];
    SessionState          state;
    GtkWidget            *terminal;   /* VteTerminal */
    GtkWidget            *dot;        /* sidebar state dot */
    GtkWidget            *name_label; /* sidebar name label */
    GtkWidget            *cwd_label;  /* sidebar cwd label */
    int                   pid;
    guint                 poll_id;
    guint                 idle_timer_id;
    guint                 cwd_timer_id;
    guint                 child_poll_id;
    int                   seen_child_pids[32];
    int                   seen_child_count;
    SessionStateChangedFn on_state_changed;
    gpointer              on_state_changed_data;
    SessionChildSpawnedFn on_child_spawned;
    gpointer              on_child_spawned_data;
    SessionChildSpawnedFn on_child_exited;
    gpointer              on_child_exited_data;
};

typedef struct {
    struct Session items[32];
    int            count;
} SessionList;

void     session_list_init(SessionList *list);
Session *session_create(SessionList *list, const char *name);
void     session_destroy(SessionList *list, int id);
void     session_set_state(Session *s, SessionState state);
void     session_spawn(Session *s, const char *cmd, const char *working_dir);
