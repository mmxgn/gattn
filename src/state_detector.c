#include "state_detector.h"
#include <glib/gfileutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <vte/vte.h>

/* Hook path: any process can write "working\n", "needs_input\n", etc. here */
static void
hook_path(Session *s, char *buf, gsize len)
{
    g_snprintf(buf, len, "%s/gattn/%d.state", g_get_user_runtime_dir(), s->pid);
}

static gboolean
poll_hook_file(gpointer data)
{
    Session *s = data;
    if (s->pid == 0)
        return G_SOURCE_CONTINUE;

    char path[512];
    hook_path(s, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f)
        return G_SOURCE_CONTINUE;

    char state[32] = { 0 };
    if (fgets(state, sizeof(state), f))
        state[strcspn(state, "\n")] = '\0';
    fclose(f);

    if (!strcmp(state, "working"))
        session_set_state(s, SESSION_WORKING);
    else if (!strcmp(state, "needs_input"))
        session_set_state(s, SESSION_NEEDS_INPUT);
    else if (!strcmp(state, "idle"))
        session_set_state(s, SESSION_IDLE);
    else if (!strcmp(state, "done"))
        session_set_state(s, SESSION_DONE);

    return G_SOURCE_CONTINUE;
}

static gboolean
on_silence(gpointer data)
{
    Session *s       = data;
    s->idle_timer_id = 0;
    session_set_state(s, SESSION_NEEDS_INPUT);
    return G_SOURCE_REMOVE;
}

static void
on_user_input(VteTerminal *term, const char *text, guint len, gpointer data)
{
    (void)term;
    Session *s = data;
    for (guint i = 0; i < len; i++) {
        if (text[i] == '\r' || text[i] == '\n')
            return; /* Enter: don't suppress — Claude is about to start working */
    }
    s->last_user_input_us = g_get_monotonic_time();
}

static void
on_contents_changed(VteTerminal *term, gpointer data)
{
    (void)term;
    Session *s = data;
    /* Ignore echoes of user keystrokes (within 500 ms of a non-Enter commit) */
    if (g_get_monotonic_time() - s->last_user_input_us < 500000)
        return;
    session_set_state(s, SESSION_WORKING);
    if (s->idle_timer_id)
        g_source_remove(s->idle_timer_id);
    s->idle_timer_id = g_timeout_add_seconds(2, on_silence, s);
}

/* Read branch name from .git/HEAD by walking up from dir. Returns g_malloc'd
   string (caller frees) or NULL if not in a git repo or HEAD is detached. */
static char *
git_branch_for(const char *cwd)
{
    char dir[256];
    g_strlcpy(dir, cwd, sizeof(dir));
    for (;;) {
        char  head[272];
        char *content = NULL;
        g_snprintf(head, sizeof(head), "%s/.git/HEAD", dir);
        if (g_file_get_contents(head, &content, NULL, NULL)) {
            char       *branch = NULL;
            const char *prefix = "ref: refs/heads/";
            if (g_str_has_prefix(content, prefix)) {
                char *nl = strchr(content + strlen(prefix), '\n');
                if (nl)
                    *nl = '\0';
                branch = g_strdup(content + strlen(prefix));
            }
            g_free(content);
            return branch;
        }
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir)
            break;
        *slash = '\0';
    }
    return NULL;
}

static void
maybe_rename_to_branch(Session *s)
{
    if (s->user_renamed)
        return;
    char *branch = git_branch_for(s->cwd);
    if (!branch)
        return;
    if (strcmp(branch, s->name) != 0) {
        g_strlcpy(s->name, branch, sizeof(s->name));
        if (s->name_label)
            gtk_label_set_text(GTK_LABEL(s->name_label), s->name);
        session_refresh_a11y(s);
    }
    g_free(branch);
}

static gboolean
poll_cwd(gpointer data)
{
    Session *s = data;
    if (s->pid == 0)
        return G_SOURCE_CONTINUE;

    char path[64];
    g_snprintf(path, sizeof(path), "/proc/%d/cwd", s->pid);
    char *link = g_file_read_link(path, NULL);
    if (!link)
        return G_SOURCE_CONTINUE;

    if (strcmp(link, s->cwd) != 0) {
        g_strlcpy(s->cwd, link, sizeof(s->cwd));
        if (s->cwd_label) {
            const char *base = strrchr(s->cwd, '/');
            gtk_label_set_text(GTK_LABEL(s->cwd_label), (base && base[1]) ? base + 1 : s->cwd);
        }
        session_refresh_a11y(s);
    }
    g_free(link);

    maybe_rename_to_branch(s);
    return G_SOURCE_CONTINUE;
}

static gboolean
poll_proc_state(gpointer data)
{
    Session *s = data;
    if (s->pid == 0)
        return G_SOURCE_CONTINUE;

    char path[64];
    g_snprintf(path, sizeof(path), "/proc/%d/stat", s->pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        session_set_state(s, SESSION_DONE);
        return G_SOURCE_CONTINUE;
    }
    /* field 3 is the state char, after the comm field in parens */
    int  pid;
    char comm[256];
    char state;
    fscanf(f, "%d %255s %c", &pid, comm, &state);
    fclose(f);

    if (state == 'R') {
        session_set_state(s, SESSION_WORKING);
    } else if (state == 'Z') {
        session_set_state(s, SESSION_DONE);
    } else {
        /* distinguish "blocked on stdin read" (permission prompt) from other sleeps */
        char spath[64];
        g_snprintf(spath, sizeof(spath), "/proc/%d/syscall", s->pid);
        char        *sc   = NULL;
        SessionState next = SESSION_NEEDS_INPUT;
        if (g_file_get_contents(spath, &sc, NULL, NULL) && sc) {
            long nr = -1, fd = -1;
            if (sscanf(sc, "%ld %lx", &nr, &fd) == 2 && nr == __NR_read && fd == 0)
                next = SESSION_BLOCKED;
            g_free(sc);
        }
        session_set_state(s, next);
    }

    return G_SOURCE_CONTINUE;
}

static gboolean
poll_children(gpointer data)
{
    Session *s = data;
    if (s->pid == 0)
        return G_SOURCE_CONTINUE;

    /* collect current child PIDs from /proc/<pid>/task/<tid>/children */
    int  cur[32], cur_count = 0;
    char task_dir[64];
    g_snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", s->pid);

    GDir *dir = g_dir_open(task_dir, 0, NULL);
    if (dir) {
        const char *tid;
        while ((tid = g_dir_read_name(dir)) != NULL) {
            char path[128];
            g_snprintf(path, sizeof(path), "%s/%s/children", task_dir, tid);
            char *buf = NULL;
            if (!g_file_get_contents(path, &buf, NULL, NULL))
                continue;
            char *p = buf;
            while (*p && cur_count < 32) {
                char *end;
                long  pid = strtol(p, &end, 10);
                if (end == p)
                    break;
                if (pid > 0)
                    cur[cur_count++] = (int)pid;
                p = end;
                while (*p == ' ' || *p == '\n')
                    p++;
            }
            g_free(buf);
        }
        g_dir_close(dir);
    }

    /* new children: in cur but not in seen */
    if (s->on_child_spawned) {
        for (int i = 0; i < cur_count; i++) {
            gboolean known = FALSE;
            for (int j = 0; j < s->seen_child_count; j++)
                if (s->seen_child_pids[j] == cur[i]) {
                    known = TRUE;
                    break;
                }
            if (!known)
                s->on_child_spawned(cur[i], s->id, s->on_child_spawned_data);
        }
    }

    /* gone children: in seen but not in cur */
    if (s->on_child_exited) {
        for (int j = 0; j < s->seen_child_count; j++) {
            gboolean still_here = FALSE;
            for (int i = 0; i < cur_count; i++)
                if (cur[i] == s->seen_child_pids[j]) {
                    still_here = TRUE;
                    break;
                }
            if (!still_here)
                s->on_child_exited(s->seen_child_pids[j], s->id, s->on_child_exited_data);
        }
    }

    /* update seen */
    s->seen_child_count = cur_count;
    memcpy(s->seen_child_pids, cur, (size_t)cur_count * sizeof(int));

    return G_SOURCE_CONTINUE;
}

void
state_detector_start_cwd(Session *s)
{
    s->cwd_timer_id = g_timeout_add_seconds(3, poll_cwd, s);
}

void
state_detector_start_child(Session *s)
{
    s->poll_id      = g_timeout_add_seconds(1, poll_proc_state, s);
    s->cwd_timer_id = g_timeout_add_seconds(3, poll_cwd, s);
}

void
state_detector_start(Session *s)
{
    char dir[480];
    g_snprintf(dir, sizeof(dir), "%s/gattn", g_get_user_runtime_dir());
    g_mkdir_with_parents(dir, 0700);

    g_signal_connect(s->terminal, "commit", G_CALLBACK(on_user_input), s);
    g_signal_connect(s->terminal, "contents-changed", G_CALLBACK(on_contents_changed), s);
    s->poll_id       = g_timeout_add_seconds(1, poll_hook_file, s);
    s->cwd_timer_id  = g_timeout_add_seconds(3, poll_cwd, s);
    s->child_poll_id = g_timeout_add_seconds(2, poll_children, s);
}

void
state_detector_stop(Session *s)
{
    if (s->poll_id) {
        g_source_remove(s->poll_id);
        s->poll_id = 0;
    }
    if (s->idle_timer_id) {
        g_source_remove(s->idle_timer_id);
        s->idle_timer_id = 0;
    }
    if (s->cwd_timer_id) {
        g_source_remove(s->cwd_timer_id);
        s->cwd_timer_id = 0;
    }
    if (s->child_poll_id) {
        g_source_remove(s->child_poll_id);
        s->child_poll_id = 0;
    }
}
