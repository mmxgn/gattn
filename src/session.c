#include "session.h"
#include <string.h>
#include <vte/vte.h>

void
session_list_init(SessionList *list)
{
    list->count = 0;
}

Session *
session_create(SessionList *list, const char *name)
{
    if (list->count >= 32)
        return NULL;
    Session *s               = &list->items[list->count++];
    s->id                    = list->count;
    s->parent_id             = 0;
    s->state                 = SESSION_IDLE;
    s->terminal              = NULL;
    s->dot                   = NULL;
    s->cwd_label             = NULL;
    s->cmd[0]                = '\0';
    s->cwd[0]                = '\0';
    s->pid                   = 0;
    s->poll_id               = 0;
    s->idle_timer_id         = 0;
    s->cwd_timer_id          = 0;
    s->child_poll_id         = 0;
    s->on_state_changed      = NULL;
    s->on_state_changed_data = NULL;
    s->seen_child_count      = 0;
    s->on_child_spawned      = NULL;
    s->on_child_spawned_data = NULL;
    s->on_child_exited       = NULL;
    s->on_child_exited_data  = NULL;
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    return s;
}

void
session_destroy(SessionList *list, int id)
{
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].id != id)
            continue;
        for (int j = i; j < list->count - 1; j++)
            list->items[j] = list->items[j + 1];
        list->count--;
        return;
    }
}

static const char *const dot_classes[] = {
    [SESSION_IDLE]        = "dot-idle",
    [SESSION_WORKING]     = "dot-working",
    [SESSION_NEEDS_INPUT] = "dot-needs-input",
    [SESSION_DONE]        = "dot-done",
};

void
session_set_state(Session *s, SessionState state)
{
    if (s->state == state)
        return;
    if (s->dot) {
        gtk_widget_remove_css_class(s->dot, dot_classes[s->state]);
        gtk_widget_add_css_class(s->dot, dot_classes[state]);
    }
    s->state = state;
    if (s->on_state_changed)
        s->on_state_changed(s, s->on_state_changed_data);
}

static void
on_child_exited(VteTerminal *term, int status, gpointer data)
{
    (void)term;
    (void)status;
    session_set_state((Session *)data, SESSION_DONE);
}

static void
on_spawn_done(VteTerminal *term, GPid pid, GError *err, gpointer data)
{
    (void)term;
    Session *s = data;
    if (err) {
        session_set_state(s, SESSION_DONE);
        return;
    }
    s->pid = (int)pid;
}

static void
on_copy_action(GSimpleAction *a, GVariant *p, gpointer term)
{
    (void)a;
    (void)p;
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(term), VTE_FORMAT_TEXT);
}

static void
on_paste_action(GSimpleAction *a, GVariant *p, gpointer term)
{
    (void)a;
    (void)p;
    vte_terminal_paste_clipboard(VTE_TERMINAL(term));
}

static void
on_right_click(GtkGestureClick *gesture, int n, double x, double y, gpointer popover)
{
    (void)n;
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

void
session_spawn(Session *s, const char *cmd, const char *working_dir)
{
    GtkWidget *term = vte_terminal_new();
    s->terminal     = term;

    GSimpleActionGroup *ag        = g_simple_action_group_new();
    GSimpleAction      *copy_act  = g_simple_action_new("copy", NULL);
    GSimpleAction      *paste_act = g_simple_action_new("paste", NULL);
    g_signal_connect(copy_act, "activate", G_CALLBACK(on_copy_action), term);
    g_signal_connect(paste_act, "activate", G_CALLBACK(on_paste_action), term);
    g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(copy_act));
    g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(paste_act));
    g_object_unref(copy_act);
    g_object_unref(paste_act);
    gtk_widget_insert_action_group(term, "term", G_ACTION_GROUP(ag));
    g_object_unref(ag);

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Copy", "term.copy");
    g_menu_append(menu, "Paste", "term.paste");
    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_widget_set_parent(popover, term);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_right_click), popover);
    gtk_widget_add_controller(term, GTK_EVENT_CONTROLLER(click));

    char **argv = NULL;
    if (cmd && *cmd) {
        GError *err = NULL;
        if (!g_shell_parse_argv(cmd, NULL, &argv, &err)) {
            g_clear_error(&err);
            argv    = g_new(char *, 2);
            argv[0] = g_strdup(cmd);
            argv[1] = NULL;
        }
    } else {
        const char *sh = g_getenv("SHELL");
        if (!sh)
            sh = "/bin/sh";
        argv    = g_new(char *, 2);
        argv[0] = g_strdup(sh);
        argv[1] = NULL;
    }

    g_signal_connect(term, "child-exited", G_CALLBACK(on_child_exited), s);
    vte_terminal_spawn_async(VTE_TERMINAL(term), VTE_PTY_DEFAULT, working_dir, argv, NULL,
                             G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, on_spawn_done, s);
    g_strfreev(argv);
}

#ifdef SESSION_TEST
#include <assert.h>
#include <stdio.h>
int
main(void)
{
    SessionList list;
    session_list_init(&list);
    assert(list.count == 0);

    Session *s = session_create(&list, "claude");
    assert(s && list.count == 1 && s->state == SESSION_IDLE);

    session_set_state(s, SESSION_WORKING);
    assert(s->state == SESSION_WORKING);

    int id = s->id;
    session_destroy(&list, id);
    assert(list.count == 0);

    puts("session: ok");
    return 0;
}
#endif
