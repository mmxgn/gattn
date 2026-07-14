#define _GNU_SOURCE 1
#include "session.h"
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vte/vte.h>

/* PCRE2 compile flags (stable ABI values) — avoids pulling in <pcre2.h>. */
#define GATTN_PCRE2_CASELESS 0x00000008u
#define GATTN_PCRE2_MULTILINE 0x00000400u

/* URLs, absolute paths, and multi-segment relative paths (foo/bar, ./x, ../x, ~/x).
   Trailing punctuation is trimmed at open time. */
static const char URL_PATH_REGEX[] = "(?:(?:https?|ftp|file)://|www\\.)[^\\s<>\"'`]+"
                                     "|~?/[^\\s<>\"'`]+"
                                     "|\\.{1,2}/[^\\s<>\"'`]+"
                                     "|[A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+";

void
session_list_init(SessionList *list)
{
    memset(list, 0, sizeof(*list));
}

/* Anything the auto-namer treats as "no real name given". Sub-agent /proc/comm
   names for common interpreters land here too. */
static const char *const GENERIC_NAMES[]
    = { "",     "shell", "claude", "agent",  "bash",   "sh",      "zsh", "fish",
        "dash", "ksh",   "node",   "nodejs", "python", "python3", NULL };

static const char *const ADJECTIVES[]
    = { "atomic", "brave",  "calm",   "clever",  "cosmic", "curious", "daring",  "eager",
        "fuzzy",  "gentle", "giddy",  "glowing", "happy",  "jolly",   "jumpy",   "keen",
        "lively", "lucky",  "mellow", "merry",   "nimble", "quirky",  "radiant", "silly",
        "snappy", "sunny",  "swift",  "tiny",    "vivid",  "witty",   "zesty" };

static const char *const ANIMALS[]
    = { "badger",   "cat",      "crab",    "dolphin", "eagle",   "ferret", "fox",   "gecko",
        "hedgehog", "hippo",    "koala",   "lynx",    "mole",    "moose",  "newt",  "otter",
        "owl",      "panda",    "penguin", "quokka",  "raccoon", "seal",   "shark", "sloth",
        "sparrow",  "squirrel", "tiger",   "turtle",  "walrus",  "wombat", "yak" };

static gboolean
is_generic_name(const char *name)
{
    if (!name)
        return TRUE;
    for (int i = 0; GENERIC_NAMES[i]; i++)
        if (g_ascii_strcasecmp(name, GENERIC_NAMES[i]) == 0)
            return TRUE;
    return FALSE;
}

/* Whimsical name based on the session's spawn order, e.g. atomic-hedgehog-3.
   The suffix ensures uniqueness even when adj/animal collide. */
static void
auto_name(char *out, size_t n, int seq)
{
    const char *adj  = ADJECTIVES[g_random_int_range(0, G_N_ELEMENTS(ADJECTIVES))];
    const char *anim = ANIMALS[g_random_int_range(0, G_N_ELEMENTS(ANIMALS))];
    g_snprintf(out, n, "%s-%s-%d", adj, anim, seq);
}

Session *
session_create(SessionList *list, const char *name)
{
    int slot = -1;
    for (int i = 0; i < 32; i++)
        if (!list->items[i]) {
            slot = i;
            break;
        }
    if (slot < 0)
        return NULL;

    Session *s = g_new0(Session, 1);
    s->id      = ++list->next_id;
    if (is_generic_name(name)) {
        auto_name(s->name, sizeof(s->name), s->id);
    } else {
        strncpy(s->name, name, sizeof(s->name) - 1);
        s->name[sizeof(s->name) - 1] = '\0';
    }

    list->items[slot] = s;
    if (slot + 1 > list->count)
        list->count = slot + 1;
    return s;
}

void
session_destroy(SessionList *list, int id)
{
    for (int i = 0; i < list->count; i++) {
        if (list->items[i] && list->items[i]->id == id) {
            g_free(list->items[i]);
            list->items[i] = NULL;
            while (list->count > 0 && !list->items[list->count - 1])
                list->count--;
            return;
        }
    }
}

static const char *const dot_classes[] = {
    [SESSION_IDLE]        = "dot-idle",
    [SESSION_WORKING]     = "dot-working",
    [SESSION_NEEDS_INPUT] = "dot-needs-input",
    [SESSION_BLOCKED]     = "dot-blocked",
    [SESSION_DONE]        = "dot-done",
};

static const char *const frame_classes[] = {
    [SESSION_IDLE]        = "frame-idle",
    [SESSION_WORKING]     = "frame-working",
    [SESSION_NEEDS_INPUT] = "frame-needs-input",
    [SESSION_BLOCKED]     = "frame-blocked",
    [SESSION_DONE]        = "frame-done",
};

static const char *
state_name(SessionState st)
{
    switch (st) {
    case SESSION_IDLE:
        return "idle";
    case SESSION_WORKING:
        return "working";
    case SESSION_NEEDS_INPUT:
        return "needs input";
    case SESSION_BLOCKED:
        return "blocked";
    case SESSION_DONE:
        return "done";
    }
    return "";
}

void
session_refresh_a11y(Session *s)
{
    if (!s || !s->dot)
        return;
    gtk_accessible_update_property(GTK_ACCESSIBLE(s->dot), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   state_name(s->state), -1);
    GtkWidget *row = gtk_widget_get_ancestor(s->dot, GTK_TYPE_LIST_BOX_ROW);
    if (row) {
        const char *base     = s->cwd[0] ? strrchr(s->cwd, '/') : NULL;
        const char *cwd_base = (base && base[1]) ? base + 1 : (s->cwd[0] ? s->cwd : "~");
        char        buf[384];
        if (s->branch[0])
            g_snprintf(buf, sizeof(buf), "%s, %s, %s, on branch %s", s->name, state_name(s->state),
                       cwd_base, s->branch);
        else
            g_snprintf(buf, sizeof(buf), "%s, %s, %s", s->name, state_name(s->state), cwd_base);
        gtk_accessible_update_property(GTK_ACCESSIBLE(row), GTK_ACCESSIBLE_PROPERTY_LABEL, buf, -1);
    }
}

void
session_set_grid_frame(Session *s, GtkWidget *frame)
{
    if (s->grid_frame)
        gtk_widget_remove_css_class(s->grid_frame, frame_classes[s->state]);
    s->grid_frame = frame;
    if (frame)
        gtk_widget_add_css_class(frame, frame_classes[s->state]);
}

void
session_set_state(Session *s, SessionState state)
{
    if (s->state == state)
        return;
    if (s->dot) {
        gtk_widget_remove_css_class(s->dot, dot_classes[s->state]);
        gtk_widget_add_css_class(s->dot, dot_classes[state]);
    }
    if (s->grid_frame) {
        gtk_widget_remove_css_class(s->grid_frame, frame_classes[s->state]);
        gtk_widget_add_css_class(s->grid_frame, frame_classes[state]);
    }
    s->state = state;
    session_refresh_a11y(s);
    if ((state == SESSION_NEEDS_INPUT || state == SESSION_BLOCKED) && s->dot) {
        char msg[128];
        g_snprintf(msg, sizeof(msg), state == SESSION_BLOCKED ? "%s is blocked" : "%s needs input",
                   s->name);
        gtk_accessible_announce(GTK_ACCESSIBLE(s->dot), msg,
                                GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
    }
    if (s->on_state_changed)
        s->on_state_changed(s, s->on_state_changed_data);
}

static void
on_child_exited(VteTerminal *term, int status, gpointer data)
{
    (void)term;
    (void)status;
    Session *s = data;
    /* Prevent PID-reuse: don't leave a signal target pointing at a recycled process. */
    s->pid = 0;
    session_set_state(s, SESSION_DONE);
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

/* Trim trailing punctuation people often type after URLs / paths (), ., ,, ;, :). */
static void
trim_match(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && strchr(").,;:!?]}", s[n - 1]))
        s[--n] = '\0';
}

/* Regular file (or directory) that's safe to hand to xdg-open.
   Refuses .desktop entries: gvfs treats them as executable launchers. */
static gboolean
is_safe_target(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return FALSE;
    if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
        return FALSE;
    if (g_str_has_suffix(path, ".desktop"))
        return FALSE;
    return TRUE;
}

/* Resolve a path candidate to an absolute realpath. If the input was relative
   to base_dir, require the result to stay under realpath(base_dir) so terminal
   output can't Ctrl-click us into `../../etc/passwd`. Caller frees. */
static char *
resolve_local_path(const char *raw, const char *base_dir)
{
    char *joined = NULL;
    if (raw[0] == '/') {
        joined = g_strdup(raw);
    } else if (raw[0] == '~') {
        joined = g_strdup_printf("%s%s", g_get_home_dir(), raw + 1);
    } else if (base_dir && *base_dir) {
        joined = g_strdup_printf("%s/%s", base_dir, raw);
    } else {
        return NULL;
    }
    char *real = realpath(joined, NULL);
    g_free(joined);
    if (!real)
        return NULL;

    if (raw[0] != '/' && raw[0] != '~' && base_dir && *base_dir) {
        char    *base_real = realpath(base_dir, NULL);
        gboolean inside    = FALSE;
        if (base_real) {
            size_t bl = strlen(base_real);
            inside    = g_str_has_prefix(real, base_real) && (real[bl] == '\0' || real[bl] == '/');
        }
        free(base_real);
        if (!inside) {
            free(real);
            return NULL;
        }
    }
    return real;
}

/* Resolve `match` to a launchable URI and open it. Refuses unknown URL schemes
   and unsafe file:// targets (.desktop, non-regular files, escapes from base_dir). */
static void
open_match(const char *match, const char *base_dir)
{
    if (!match || !*match)
        return;
    char *m = g_strdup(match);
    trim_match(m);

    char *uri = NULL;
    if (g_str_has_prefix(m, "http://") || g_str_has_prefix(m, "https://")
        || g_str_has_prefix(m, "ftp://")) {
        uri = g_strdup(m);
    } else if (g_str_has_prefix(m, "www.")) {
        uri = g_strdup_printf("https://%s", m);
    } else if (g_str_has_prefix(m, "file://")) {
        char *path = g_uri_unescape_string(m + 7, NULL);
        if (path && is_safe_target(path))
            uri = g_strdup_printf("file://%s", path);
        g_free(path);
    } else if (strstr(m, "://")) {
        /* Unknown scheme — refuse. Prevents `steam://…`, `custom://…`, etc. */
    } else {
        char *real = resolve_local_path(m, base_dir);
        if (real && is_safe_target(real))
            uri = g_strdup_printf("file://%s", real);
        free(real);
    }
    g_free(m);

    if (uri) {
        g_app_info_launch_default_for_uri(uri, NULL, NULL);
        g_free(uri);
    }
}

static const char *
term_cwd(GtkWidget *term)
{
    Session *s = g_object_get_data(G_OBJECT(term), "gattn-session");
    return (s && s->cwd[0]) ? s->cwd : NULL;
}

/* Return matched string at widget-local (x, y), or NULL. Caller frees. */
static char *
match_at(VteTerminal *term, double x, double y)
{
    int   tag = -1;
    char *m   = vte_terminal_check_match_at(term, x, y, &tag);
    return (m && *m) ? m : (g_free(m), NULL);
}

typedef struct {
    VteTerminal *term;
    char        *match; /* nullable */
} MenuCtx;

static void
menu_ctx_free(gpointer d)
{
    MenuCtx *c = d;
    g_free(c->match);
    g_free(c);
}

static void
on_menu_open(GtkButton *btn, gpointer data)
{
    MenuCtx   *c   = data;
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
    open_match(c->match, term_cwd(GTK_WIDGET(c->term)));
}

static void
on_menu_copy(GtkButton *btn, gpointer data)
{
    MenuCtx   *c   = data;
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
    vte_terminal_copy_clipboard_format(c->term, VTE_FORMAT_TEXT);
}

static void
on_menu_paste(GtkButton *btn, gpointer data)
{
    MenuCtx   *c   = data;
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
    vte_terminal_paste_clipboard(c->term);
}

static GtkWidget *
menu_row(const char *icon, const char *label, GCallback cb, MenuCtx *ctx, gboolean own_ctx)
{
    GtkWidget *btn  = gtk_button_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(hbox), gtk_image_new_from_icon_name(icon));
    GtkWidget *lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 40);
    gtk_box_append(GTK_BOX(hbox), lbl);
    gtk_button_set_child(GTK_BUTTON(btn), hbox);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_halign(btn, GTK_ALIGN_FILL);
    if (own_ctx)
        g_signal_connect_data(btn, "clicked", cb, ctx, (GClosureNotify)menu_ctx_free, 0);
    else
        g_signal_connect(btn, "clicked", cb, ctx);
    return btn;
}

static void
on_right_click(GtkGestureClick *gesture, int n, double x, double y, gpointer term_ptr)
{
    (void)n;
    VteTerminal *term = term_ptr;

    MenuCtx *ctx = g_new0(MenuCtx, 1);
    ctx->term    = term;
    ctx->match   = match_at(term, x, y);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (ctx->match) {
        char label[128];
        g_snprintf(label, sizeof(label), "Open  %s", ctx->match);
        gtk_box_append(GTK_BOX(vbox), menu_row("document-open-symbolic", label,
                                               G_CALLBACK(on_menu_open), ctx, FALSE));
        gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    }
    gtk_box_append(GTK_BOX(vbox),
                   menu_row("edit-copy-symbolic", "Copy", G_CALLBACK(on_menu_copy), ctx, FALSE));
    gtk_box_append(GTK_BOX(vbox),
                   menu_row("edit-paste-symbolic", "Paste", G_CALLBACK(on_menu_paste), ctx, TRUE));

    GtkWidget *pop = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(pop), vbox);
    gtk_widget_set_parent(pop, GTK_WIDGET(term));
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    g_signal_connect_swapped(pop, "closed", G_CALLBACK(gtk_widget_unparent), pop);
    gtk_popover_popup(GTK_POPOVER(pop));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

/* Ctrl+left-click on a match opens it (bare click stays for text selection). */
static void
on_primary_press(GtkGestureClick *gesture, int n, double x, double y, gpointer term_ptr)
{
    (void)n;
    GdkModifierType mods
        = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    if (!(mods & GDK_CONTROL_MASK))
        return;
    char *m = match_at(VTE_TERMINAL(term_ptr), x, y);
    if (m) {
        open_match(m, term_cwd(GTK_WIDGET(term_ptr)));
        g_free(m);
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }
}

void
session_spawn(Session *s, const char *cmd, const char *working_dir)
{
    GtkWidget *term = vte_terminal_new();
    s->terminal     = term;
    g_object_set_data(G_OBJECT(term), "gattn-session", s);

    /* Register URL/path regex so VTE highlights matches + shows the pointer cursor. */
    VteRegex *regex = vte_regex_new_for_match(URL_PATH_REGEX, -1,
                                              GATTN_PCRE2_CASELESS | GATTN_PCRE2_MULTILINE, NULL);
    if (regex) {
        int tag = vte_terminal_match_add_regex(VTE_TERMINAL(term), regex, 0);
        vte_terminal_match_set_cursor_name(VTE_TERMINAL(term), tag, "pointer");
        vte_regex_unref(regex);
    }

    GtkGesture *rc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), GDK_BUTTON_SECONDARY);
    g_signal_connect(rc, "pressed", G_CALLBACK(on_right_click), term);
    gtk_widget_add_controller(term, GTK_EVENT_CONTROLLER(rc));

    GtkGesture *lc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(lc), GDK_BUTTON_PRIMARY);
    g_signal_connect(lc, "pressed", G_CALLBACK(on_primary_press), term);
    gtk_widget_add_controller(term, GTK_EVENT_CONTROLLER(lc));

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
