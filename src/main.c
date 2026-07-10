#define _POSIX_C_SOURCE 200809L
#include "notify.h"
#include "prefs_store.h"
#include "recents.h"
#include "session.h"
#include "sessions_store.h"
#include "state_detector.h"
#include "ui/grid.h"
#include "ui/prefs.h"
#include "ui/session_picker.h"
#include "ui/sidebar.h"
#include <adwaita.h>
#include <signal.h>
#include <string.h>
#include <vte/vte.h>

static SessionList sessions;

static const char CSS[] = ".dot-idle        { color: gray;    }"
                          ".dot-working     { color: #4CAF50; }"
                          ".dot-needs-input { color: #FFC107; }"
                          ".dot-done        { color: #2196F3; }"
                          "";

static struct {
    GtkWidget       *outer_stack;
    GtkWidget       *split;
    AdwToastOverlay *overlay;
    GApplication    *gapp;
} app;

static GSimpleAction   *exit_grid_act;
static GtkToggleButton *grid_btn;

static void apply_prefs_to_terminal(VteTerminal *t, const GattnPrefs *p);

/* ── helpers ── */

static GtkListBox *
get_lb(void)
{
    return GTK_LIST_BOX(g_object_get_data(G_OBJECT(app.split), "gattn-listbox"));
}

static Session *
selected_session(void)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(get_lb());
    return row ? g_object_get_data(G_OBJECT(row), "gattn-session") : NULL;
}

/* ── grid toggle (keeps exit-grid action in sync) ── */

static void toggle_grid(void); /* forward declaration */

static void
on_grid_back(GtkButton *btn, gpointer d)
{
    (void)btn;
    (void)d;
    toggle_grid();
}

static void
toggle_grid(void)
{
    grid_toggle(app.outer_stack, app.split, &sessions, G_CALLBACK(on_grid_back), NULL);
    const char *cur     = gtk_stack_get_visible_child_name(GTK_STACK(app.outer_stack));
    gboolean    in_grid = cur && strcmp(cur, "grid") == 0;
    g_simple_action_set_enabled(exit_grid_act, in_grid);
    if (grid_btn)
        gtk_toggle_button_set_active(grid_btn, in_grid);
}

/* ── action callbacks ── */

static void
on_toggle_grid(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    toggle_grid();
}

static void
on_exit_grid(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    toggle_grid();
}

static void
on_raise(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(d));
    if (win)
        gtk_window_present(win);
}

static void
on_close_session(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    Session *s = selected_session();
    if (s && s->pid > 0)
        kill(s->pid, SIGHUP);
}

static void
on_move_session(int delta)
{
    GtkListBox *lb = get_lb();

    /* collect top-level session rows only */
    GtkListBoxRow *tops[32];
    int            ntops = 0;
    for (int i = 0; ntops < 32; i++) {
        GtkListBoxRow *r = gtk_list_box_get_row_at_index(lb, i);
        if (!r)
            break;
        Session *s = g_object_get_data(G_OBJECT(r), "gattn-session");
        if (s && s->parent_id == 0)
            tops[ntops++] = r;
    }
    if (!ntops)
        return;

    /* find current top-level position (handle subagent rows too) */
    GtkListBoxRow *sel = gtk_list_box_get_selected_row(lb);
    int            cur = 0;
    if (sel) {
        Session *ss        = g_object_get_data(G_OBJECT(sel), "gattn-session");
        int      target_id = ss ? (ss->parent_id ? ss->parent_id : ss->id) : -1;
        for (int i = 0; i < ntops; i++) {
            Session *ts = g_object_get_data(G_OBJECT(tops[i]), "gattn-session");
            if (ts && ts->id == target_id) {
                cur = i;
                break;
            }
        }
    }

    int next = ((cur + delta) % ntops + ntops) % ntops;
    gtk_list_box_select_row(lb, tops[next]);
}

static void
on_next_session(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    on_move_session(1);
}

static void
on_prev_session(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    on_move_session(-1);
}

static void
on_jump_first(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    GtkListBox *lb = get_lb();
    for (int i = 0;; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, i);
        if (!row)
            break;
        Session *s = g_object_get_data(G_OBJECT(row), "gattn-session");
        if (s && s->parent_id == 0) {
            gtk_list_box_select_row(lb, row);
            break;
        }
    }
}

static void
on_jump_last(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    GtkListBox    *lb   = get_lb();
    GtkListBoxRow *last = NULL;
    for (int i = 0;; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, i);
        if (!row)
            break;
        Session *s = g_object_get_data(G_OBJECT(row), "gattn-session");
        if (s && s->parent_id == 0)
            last = row;
    }
    if (last)
        gtk_list_box_select_row(lb, last);
}

static void
on_next_unattended(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    GtkListBox    *lb    = get_lb();
    GtkListBoxRow *cur   = gtk_list_box_get_selected_row(lb);
    int            start = cur ? gtk_list_box_row_get_index(cur) + 1 : 0;
    int            n     = sessions.count;
    for (int i = 0; i < n; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, (start + i) % n);
        if (!row)
            continue;
        Session *s = g_object_get_data(G_OBJECT(row), "gattn-session");
        if (s && s->state == SESSION_NEEDS_INPUT) {
            gtk_list_box_select_row(lb, row);
            return;
        }
    }
}

/* ── sub-agent detection ── */

static void
on_child_spawned(int child_pid, int parent_id, gpointer data)
{
    (void)data;
    /* already tracked? */
    for (int i = 0; i < sessions.count; i++)
        if (sessions.items[i].pid == child_pid)
            return;

    /* find parent */
    Session *parent = NULL;
    for (int i = 0; i < sessions.count; i++)
        if (sessions.items[i].id == parent_id) {
            parent = &sessions.items[i];
            break;
        }
    if (!parent)
        return;

    /* read child process name from /proc/<pid>/comm */
    char comm_path[64], name[64] = "agent";
    g_snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", child_pid);
    char *comm = NULL;
    if (g_file_get_contents(comm_path, &comm, NULL, NULL)) {
        g_strstrip(comm);
        g_strlcpy(name, comm, sizeof(name));
        g_free(comm);
    }

    Session *child = session_create(&sessions, name);
    if (!child)
        return;
    child->parent_id = parent_id;
    child->pid       = child_pid;
    child->terminal  = parent->terminal;
    if (parent->cwd[0])
        g_strlcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    sidebar_add_session(app.split, child);
    state_detector_start_cwd(child);
}

static void
on_child_exited(int child_pid, int parent_id, gpointer data)
{
    (void)parent_id;
    (void)data;
    for (int i = 0; i < sessions.count; i++) {
        if (sessions.items[i].pid == child_pid && sessions.items[i].parent_id != 0) {
            int id = sessions.items[i].id;
            state_detector_stop(&sessions.items[i]);
            sidebar_remove_session(app.split, id);
            session_destroy(&sessions, id);
            return;
        }
    }
}

/* ── session cleanup (fires after session.c's own child-exited handler) ── */

static void
on_session_cleanup(GtkWidget *term, int status, gpointer data)
{
    (void)term;
    (void)status;
    Session *s  = data;
    int      id = s->id;

    /* stop detector before any array shifts */
    state_detector_stop(s);

    /* collect and remove child sessions */
    int child_ids[32], child_count = 0;
    for (int i = 0; i < sessions.count; i++)
        if (sessions.items[i].parent_id == id && child_count < 32)
            child_ids[child_count++] = sessions.items[i].id;
    for (int i = 0; i < child_count; i++) {
        for (int j = 0; j < sessions.count; j++)
            if (sessions.items[j].id == child_ids[i]) {
                state_detector_stop(&sessions.items[j]);
                break;
            }
        sidebar_remove_session(app.split, child_ids[i]);
        session_destroy(&sessions, child_ids[i]);
    }

    sidebar_remove_session(app.split, id);
    session_destroy(&sessions, id);
    sessions_save(&sessions);
}

/* ── session creation ── */

static void
add_session(GtkWidget *split, const char *cmd, const char *working_dir)
{
    char name[64] = "shell";
    if (cmd && *cmd) {
        char tmp[256];
        g_strlcpy(tmp, cmd, sizeof(tmp));
        char *space = strchr(tmp, ' ');
        if (space)
            *space = '\0';
        const char *base = strrchr(tmp, '/');
        g_strlcpy(name, base ? base + 1 : tmp, sizeof(name));
    }
    Session *s = session_create(&sessions, name);
    if (!s)
        return;
    s->on_child_spawned      = on_child_spawned;
    s->on_child_spawned_data = NULL;
    s->on_child_exited       = on_child_exited;
    s->on_child_exited_data  = NULL;
    if (cmd && *cmd)
        g_strlcpy(s->cmd, cmd, sizeof(s->cmd));
    if (working_dir) {
        g_strlcpy(s->cwd, working_dir, sizeof(s->cwd));
        recents_add(working_dir);
    }
    session_spawn(s, (cmd && *cmd) ? cmd : NULL, working_dir);
    GattnPrefs prefs = prefs_load();
    apply_prefs_to_terminal(VTE_TERMINAL(s->terminal), &prefs);
    g_signal_connect(s->terminal, "child-exited", G_CALLBACK(on_session_cleanup), s);
    state_detector_start(s);
    notify_watch(s, app.overlay, app.gapp);
    sidebar_add_session(split, s);
    sessions_save(&sessions);
}

static void
on_session_picked(const char *cmd, const char *dir, gpointer data)
{
    add_session((GtkWidget *)data, cmd, dir);
}

static void
on_new_session(GtkWidget *split, gpointer data)
{
    (void)data;
    GtkWindow  *win = gtk_application_get_active_window(GTK_APPLICATION(app.gapp));
    Session    *s   = selected_session();
    const char *cwd = (s && s->cwd[0]) ? s->cwd : NULL;
    session_picker_show(GTK_WIDGET(win), on_session_picked, split, cwd);
}

static void
apply_prefs_to_terminal(VteTerminal *t, const GattnPrefs *p)
{
    PangoFontDescription *fd = pango_font_description_from_string(p->font);
    vte_terminal_set_font(t, fd);
    pango_font_description_free(fd);
    GdkRGBA fg = { 1, 1, 1, 1 }, bg = { 0, 0, 0, 1 };
    gdk_rgba_parse(&fg, p->fg);
    gdk_rgba_parse(&bg, p->bg);
    vte_terminal_set_colors(t, &fg, &bg, NULL, 0);
}

static void
on_prefs_changed(gpointer data)
{
    (void)data;
    GattnPrefs p = prefs_load();
    for (int i = 0; i < sessions.count; i++)
        if (sessions.items[i].terminal)
            apply_prefs_to_terminal(VTE_TERMINAL(sessions.items[i].terminal), &p);
}

static void
on_preferences(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app.gapp));
    prefs_dialog_show(GTK_WIDGET(win), on_prefs_changed, NULL);
}

static void
on_about(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    AdwAboutDialog *dlg = ADW_ABOUT_DIALOG(adw_about_dialog_new());
    adw_about_dialog_set_application_name(dlg, "gattn");
    adw_about_dialog_set_version(dlg, "0.1.0");
    adw_about_dialog_set_comments(dlg, "GNOME attention hub for Claude Code sessions");
    adw_about_dialog_set_website(dlg, "https://github.com/mmxgn/gattn");
    adw_about_dialog_set_issue_url(dlg, "https://github.com/mmxgn/gattn/issues");
    adw_about_dialog_set_license_type(dlg, GTK_LICENSE_MIT_X11);
    const char *devs[] = { "mmxgn", NULL };
    adw_about_dialog_set_developers(dlg, devs);
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app.gapp));
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(win));
}

static void
on_rename_session(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    Session   *s   = selected_session();
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app.gapp));
    if (s && win)
        sidebar_rename_session(s, GTK_WIDGET(win));
}

static void
on_focus_sidebar(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(app.split), FALSE);
    gtk_widget_grab_focus(GTK_WIDGET(get_lb()));
}

static void
on_focus_content(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    adw_navigation_split_view_set_show_content(ADW_NAVIGATION_SPLIT_VIEW(app.split), TRUE);
    Session *s = selected_session();
    if (s)
        gtk_widget_grab_focus(s->terminal);
}

static void
on_show_diff(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    Session   *s   = selected_session();
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app.gapp));
    if (s && win)
        sidebar_show_diff(s, GTK_WIDGET(win));
}

static void
on_new_session_action(GSimpleAction *a, GVariant *p, gpointer d)
{
    (void)a;
    (void)p;
    (void)d;
    on_new_session(app.split, NULL);
}

/* ── startup ── */

static void
register_action(GActionMap *map, const char *name, GCallback cb, gpointer data)
{
    GSimpleAction *act = g_simple_action_new(name, NULL);
    g_signal_connect(act, "activate", cb, data);
    g_action_map_add_action(map, G_ACTION(act));
    g_object_unref(act);
}

static void
on_activate(AdwApplication *app_obj, gpointer data)
{
    (void)data;
    app.gapp = G_APPLICATION(app_obj);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    app.overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());

    app.split = sidebar_new(&sessions, on_new_session, NULL);

    AdwHeaderBar *content_hdr = g_object_get_data(G_OBJECT(app.split), "gattn-content-header");
    grid_btn                  = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_button_set_icon_name(GTK_BUTTON(grid_btn), "view-grid-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(grid_btn), "Grid view (Ctrl+G)");
    g_signal_connect_swapped(grid_btn, "clicked", G_CALLBACK(toggle_grid), NULL);
    adw_header_bar_pack_end(content_hdr, GTK_WIDGET(grid_btn));

    app.outer_stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(app.outer_stack), app.split, "split");
    adw_toast_overlay_set_child(app.overlay, app.outer_stack);

    GActionMap *map = G_ACTION_MAP(app_obj);
    register_action(map, "toggle-grid", G_CALLBACK(on_toggle_grid), NULL);
    register_action(map, "new-session", G_CALLBACK(on_new_session_action), NULL);
    register_action(map, "close-session", G_CALLBACK(on_close_session), NULL);
    register_action(map, "next-session", G_CALLBACK(on_next_session), NULL);
    register_action(map, "prev-session", G_CALLBACK(on_prev_session), NULL);
    register_action(map, "next-unattended", G_CALLBACK(on_next_unattended), NULL);
    register_action(map, "jump-first", G_CALLBACK(on_jump_first), NULL);
    register_action(map, "jump-last", G_CALLBACK(on_jump_last), NULL);
    register_action(map, "raise", G_CALLBACK(on_raise), app_obj);
    register_action(map, "show-diff", G_CALLBACK(on_show_diff), NULL);
    register_action(map, "focus-sidebar", G_CALLBACK(on_focus_sidebar), NULL);
    register_action(map, "focus-content", G_CALLBACK(on_focus_content), NULL);
    register_action(map, "preferences", G_CALLBACK(on_preferences), NULL);
    register_action(map, "about", G_CALLBACK(on_about), NULL);
    register_action(map, "rename-session", G_CALLBACK(on_rename_session), NULL);

    /* exit-grid is disabled until grid mode is entered */
    exit_grid_act = g_simple_action_new("exit-grid", NULL);
    g_simple_action_set_enabled(exit_grid_act, FALSE);
    g_signal_connect(exit_grid_act, "activate", G_CALLBACK(on_exit_grid), NULL);
    g_action_map_add_action(map, G_ACTION(exit_grid_act));
    g_object_unref(exit_grid_act);

    AdwApplicationWindow *win
        = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app_obj)));
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 700);
    adw_application_window_set_content(win, GTK_WIDGET(app.overlay));
    gtk_window_present(GTK_WINDOW(win));

    SavedSession saved[32];
    int          nsaved = sessions_load(saved, 32);
    if (nsaved > 0) {
        for (int i = 0; i < nsaved; i++)
            add_session(app.split, saved[i].cmd[0] ? saved[i].cmd : NULL,
                        saved[i].dir[0] ? saved[i].dir : NULL);
    } else {
        session_picker_show(GTK_WIDGET(win), on_session_picked, app.split, NULL);
    }
}

int
main(int argc, char *argv[])
{
    session_list_init(&sessions);
    AdwApplication *a = adw_application_new("org.mmxgn.gattn", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(a, "activate", G_CALLBACK(on_activate), NULL);

    struct {
        const char *action;
        const char *accel;
    } bindings[] = {
        { "app.toggle-grid", "<Control>g" },
        { "app.new-session", "<Control>n" },
        { "app.close-session", "<Control><Shift>w" },
        { "app.next-session", "<Control>Tab" },
        { "app.prev-session", "<Control><Shift>Tab" },
        { "app.next-unattended", "<Control><Shift>a" },
        { "app.jump-first", "<Control>Prior" },
        { "app.jump-last", "<Control>Next" },
        { "app.exit-grid", "Escape" },
        { "app.show-diff", "<Control><Shift>d" },
        { "app.focus-sidebar", "<Control>Left" },
        { "app.focus-content", "<Control>Right" },
        { "app.rename-session", "F2" },
    };
    for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); i++) {
        const char *accels[] = { bindings[i].accel, NULL };
        gtk_application_set_accels_for_action(GTK_APPLICATION(a), bindings[i].action, accels);
    }
    /* multi-accel overrides (must come after the loop) */
    gtk_application_set_accels_for_action(
        GTK_APPLICATION(a), "app.next-session",
        (const char *[]){ "<Control>Tab", "<Control>Down", NULL });
    gtk_application_set_accels_for_action(
        GTK_APPLICATION(a), "app.prev-session",
        (const char *[]){ "<Control><Shift>Tab", "<Control>Up", NULL });

    int status = g_application_run(G_APPLICATION(a), argc, argv);
    g_object_unref(a);
    return status;
}
