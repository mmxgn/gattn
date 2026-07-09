#include "state_detector.h"
#include <vte/vte.h>
#include <stdio.h>
#include <string.h>

/* Hook path: any process can write "working\n", "needs_input\n", etc. here */
static void hook_path(Session *s, char *buf, gsize len)
{
    g_snprintf(buf, len, "%s/gattn/%d.state", g_get_user_runtime_dir(), s->pid);
}

static gboolean poll_hook_file(gpointer data)
{
    Session *s = data;
    if (s->pid == 0)
        return G_SOURCE_CONTINUE;

    char path[512];
    hook_path(s, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f)
        return G_SOURCE_CONTINUE;

    char state[32] = {0};
    if (fgets(state, sizeof(state), f))
        state[strcspn(state, "\n")] = '\0';
    fclose(f);

    if      (!strcmp(state, "working"))     session_set_state(s, SESSION_WORKING);
    else if (!strcmp(state, "needs_input")) session_set_state(s, SESSION_NEEDS_INPUT);
    else if (!strcmp(state, "idle"))        session_set_state(s, SESSION_IDLE);
    else if (!strcmp(state, "done"))        session_set_state(s, SESSION_DONE);

    return G_SOURCE_CONTINUE;
}

static gboolean on_silence(gpointer data)
{
    Session *s = data;
    s->idle_timer_id = 0;
    if (s->state == SESSION_WORKING)
        session_set_state(s, SESSION_NEEDS_INPUT);
    return G_SOURCE_REMOVE;
}

static void on_contents_changed(VteTerminal *term, gpointer data)
{
    (void)term;
    Session *s = data;
    session_set_state(s, SESSION_WORKING);
    if (s->idle_timer_id)
        g_source_remove(s->idle_timer_id);
    s->idle_timer_id = g_timeout_add_seconds(5, on_silence, s);
}

void state_detector_start(Session *s)
{
    char dir[480];
    g_snprintf(dir, sizeof(dir), "%s/gattn", g_get_user_runtime_dir());
    g_mkdir_with_parents(dir, 0700);

    g_signal_connect(s->terminal, "contents-changed",
                     G_CALLBACK(on_contents_changed), s);
    s->poll_id = g_timeout_add_seconds(1, poll_hook_file, s);
}

void state_detector_stop(Session *s)
{
    if (s->poll_id)      { g_source_remove(s->poll_id);      s->poll_id = 0; }
    if (s->idle_timer_id){ g_source_remove(s->idle_timer_id);s->idle_timer_id = 0; }
}
