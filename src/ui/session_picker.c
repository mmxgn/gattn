#include "session_picker.h"
#include "../recents.h"
#include <adwaita.h>
#include <gio/gio.h>
#include <string.h>

typedef struct {
    AdwDialog      *dialog;
    GtkWindow      *parent_win;
    SessionPickedFn picked;
    gpointer        picked_data;
    char            cmd[128];
    char            dir[512];
} ActionData;

/* -- claude session discovery -- */

/* ~/.claude projects use the abs path with every '/' replaced by '-' */
static char *
encode_project_path(const char *abs_path)
{
    char *key = g_strdup(abs_path);
    for (char *p = key; *p; p++)
        if (*p == '/')
            *p = '-';
    return key;
}

/* Returns NULL-terminated array of session UUIDs (filenames minus .jsonl).
   Caller must g_strfreev(). */
static char **
list_claude_sessions(const char *project_dir)
{
    char *key  = encode_project_path(project_dir);
    char *path = g_strdup_printf("%s/.claude/projects/%s", g_get_home_dir(), key);
    g_free(key);

    GDir *dir = g_dir_open(path, 0, NULL);
    g_free(path);
    if (!dir)
        return NULL;

    GPtrArray  *arr = g_ptr_array_new_with_free_func(g_free);
    const char *name;
    while ((name = g_dir_read_name(dir))) {
        if (!g_str_has_suffix(name, ".jsonl"))
            continue;
        /* Refuse dotfile-shaped entries (defensive; the only real escapes in a POSIX
           filename are `.` and `..`, both of which start with `.`). */
        if (name[0] == '.')
            continue;
        g_ptr_array_add(arr, g_strndup(name, strlen(name) - 6));
    }
    g_dir_close(dir);
    g_ptr_array_add(arr, NULL);
    return (char **)g_ptr_array_free(arr, FALSE);
}

/* Fetch mtime as unix seconds + formatted "YYYY-MM-DD HH:MM" (unknown/0 on failure).
   Caller frees *out_str. */
static void
session_mtime(const char *project_dir, const char *session_id, guint64 *out_unix, char **out_str)
{
    *out_unix = 0;
    *out_str  = g_strdup("unknown");

    char *key = encode_project_path(project_dir);
    char *path
        = g_strdup_printf("%s/.claude/projects/%s/%s.jsonl", g_get_home_dir(), key, session_id);
    g_free(key);

    GFile *f = g_file_new_for_path(path);
    g_free(path);
    GFileInfo *info
        = g_file_query_info(f, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
    g_object_unref(f);
    if (!info)
        return;

    *out_unix     = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    GDateTime *dt = g_file_info_get_modification_date_time(info);
    g_object_unref(info);
    if (dt) {
        g_free(*out_str);
        *out_str = g_date_time_format(dt, "%Y-%m-%d %H:%M");
        g_date_time_unref(dt);
    }
}

/* Extract the first user message from a session's .jsonl and return it as a title.
   Scans lines for {"type":"user",...,"message":{..."content":"..."...}} and unescapes the
   common JSON string escapes we care about. Returns NULL if nothing usable found. */
static char *
session_title(const char *project_dir, const char *session_id)
{
    char *key = encode_project_path(project_dir);
    char *path
        = g_strdup_printf("%s/.claude/projects/%s/%s.jsonl", g_get_home_dir(), key, session_id);
    g_free(key);

    char    *contents = NULL;
    gsize    len      = 0;
    gboolean ok       = g_file_get_contents(path, &contents, &len, NULL);
    g_free(path);
    if (!ok)
        return NULL;

    char       *title = NULL;
    const char *p     = contents;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        gsize       ll = nl ? (gsize)(nl - p) : strlen(p);
        if (ll > 12 && strstr(p, "\"type\":\"user\"") && strstr(p, "\"role\":\"user\"")) {
            /* Find the content field within this line only. */
            const char *needle = "\"content\":\"";
            const char *c      = g_strstr_len(p, ll, needle);
            if (c) {
                c += strlen(needle);
                GString    *s   = g_string_new(NULL);
                const char *end = p + ll;
                while (c < end && *c != '"') {
                    if (*c == '\\' && c + 1 < end) {
                        switch (c[1]) {
                        case 'n':
                        case 'r':
                        case 't':
                            g_string_append_c(s, ' ');
                            break;
                        case '"':
                            g_string_append_c(s, '"');
                            break;
                        case '\\':
                            g_string_append_c(s, '\\');
                            break;
                        case '/':
                            g_string_append_c(s, '/');
                            break;
                        default:
                            /* skip unknown escapes (including \uXXXX) rather than break */
                            break;
                        }
                        c += 2;
                    } else {
                        g_string_append_c(s, *c);
                        c++;
                    }
                }
                title = g_string_free(s, FALSE);
                break;
            }
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    g_free(contents);
    if (!title || !*title) {
        g_free(title);
        return NULL;
    }
    /* Collapse whitespace and truncate for display. */
    g_strstrip(title);
    if (g_utf8_strlen(title, -1) > 70) {
        char *end = g_utf8_offset_to_pointer(title, 70);
        *end      = '\0';
        char *dot = g_strdup_printf("%s…", title);
        g_free(title);
        title = dot;
    }
    return title;
}

typedef struct {
    const char *id;
    guint64     mtime;
    char       *mtime_str;
} SessionEntry;

static int
cmp_entry_desc(const void *a, const void *b)
{
    guint64 ma = ((const SessionEntry *)a)->mtime;
    guint64 mb = ((const SessionEntry *)b)->mtime;
    return (mb > ma) - (mb < ma);
}

/* Returns the most-recent session UUID for dir, or NULL. Caller frees. */
static char *
latest_claude_session(const char *dir)
{
    char **sessions = list_claude_sessions(dir);
    if (!sessions || !sessions[0]) {
        g_strfreev(sessions);
        return NULL;
    }
    int n = 0;
    while (sessions[n])
        n++;
    SessionEntry *entries = g_new(SessionEntry, n);
    for (int i = 0; i < n; i++) {
        entries[i].id = sessions[i];
        session_mtime(dir, sessions[i], &entries[i].mtime, &entries[i].mtime_str);
    }
    qsort(entries, n, sizeof(SessionEntry), cmp_entry_desc);
    char *id = g_strdup(entries[0].id);
    for (int i = 0; i < n; i++)
        g_free(entries[i].mtime_str);
    g_free(entries);
    g_strfreev(sessions);
    return id;
}

/* Forward declarations — show_session_list needs these before they're defined */
static GtkWidget *make_action_row(const char *icon, const char *title, const char *subtitle,
                                  ActionData *ad);
static GtkWidget *make_listbox(void);
static void       on_row_activated(GtkListBox *lb, GtkListBoxRow *row, gpointer data);

static void
show_session_list(const char *dir, SessionPickedFn picked, gpointer picked_data,
                  GtkWindow *parent_win)
{
    char **sessions = list_claude_sessions(dir);

    /* One session? Skip the list — resume it directly. */
    if (sessions && sessions[0] && !sessions[1]) {
        char cmd[128];
        g_snprintf(cmd, sizeof(cmd), "claude --resume %s", sessions[0]);
        picked(cmd, dir, picked_data);
        g_strfreev(sessions);
        return;
    }

    const char *base = strrchr(dir, '/');
    const char *name = (base && base[1]) ? base + 1 : dir;
    char        title[128];
    g_snprintf(title, sizeof(title), "Resume session in %s", name);

    AdwDialog *dialog = ADW_DIALOG(adw_dialog_new());
    adw_dialog_set_title(dialog, title);
    adw_dialog_set_content_width(dialog, 540);
    adw_dialog_set_content_height(dialog, 400);

    GtkWidget *toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(inner, 16);
    gtk_widget_set_margin_end(inner, 16);
    gtk_widget_set_margin_top(inner, 16);
    gtk_widget_set_margin_bottom(inner, 16);

    if (!sessions || !sessions[0]) {
        GtkWidget *lbl = gtk_label_new("No Claude sessions found in this directory.");
        gtk_widget_add_css_class(lbl, "dim-label");
        gtk_widget_set_margin_top(lbl, 20);
        gtk_box_append(GTK_BOX(inner), lbl);
    } else {
        int n = 0;
        while (sessions[n])
            n++;
        SessionEntry *entries = g_new(SessionEntry, n);
        for (int i = 0; i < n; i++) {
            entries[i].id = sessions[i];
            session_mtime(dir, sessions[i], &entries[i].mtime, &entries[i].mtime_str);
        }
        qsort(entries, n, sizeof(SessionEntry), cmp_entry_desc);

        GtkWidget *lb = make_listbox();
        for (int i = 0; i < n; i++) {
            /* First 8 chars of UUID as compact id. */
            char short_id[40];
            g_strlcpy(short_id, entries[i].id, sizeof(short_id));
            if (strlen(short_id) > 8)
                short_id[8] = '\0';

            char *title = session_title(dir, entries[i].id);
            char  sub[256];
            g_snprintf(sub, sizeof(sub), "%s · %s", short_id, entries[i].mtime_str);

            char cmd[128];
            g_snprintf(cmd, sizeof(cmd), "claude --resume %s", entries[i].id);

            ActionData *ad  = g_new(ActionData, 1);
            ad->dialog      = dialog;
            ad->parent_win  = parent_win;
            ad->picked      = picked;
            ad->picked_data = picked_data;
            g_strlcpy(ad->cmd, cmd, sizeof(ad->cmd));
            g_strlcpy(ad->dir, dir, sizeof(ad->dir));

            gtk_list_box_append(GTK_LIST_BOX(lb),
                                make_action_row("document-open-recent-symbolic",
                                                title ? title : short_id, sub, ad));
            g_free(title);
            g_free(entries[i].mtime_str);
        }
        g_free(entries);
        g_signal_connect(lb, "row-activated", G_CALLBACK(on_row_activated), NULL);
        gtk_box_append(GTK_BOX(inner), lb);
    }
    g_strfreev(sessions);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), inner);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), scroll);
    adw_dialog_set_child(dialog, toolbar);
    adw_dialog_present(dialog, GTK_WIDGET(parent_win));
}

static void
fire_and_close(ActionData *ad)
{
    ad->picked(ad->cmd[0] ? ad->cmd : NULL, ad->dir[0] ? ad->dir : NULL, ad->picked_data);
    adw_dialog_close(ad->dialog);
}

static void
on_folder_chosen(GObject *src, GAsyncResult *res, gpointer data)
{
    ActionData *ad   = data;
    GFile      *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        g_object_unref(file);
        if (path) {
            if (strcmp(ad->cmd, "claude --continue") == 0)
                show_session_list(path, ad->picked, ad->picked_data, ad->parent_win);
            else
                ad->picked(ad->cmd[0] ? ad->cmd : NULL, path, ad->picked_data);
            g_free(path);
        }
    }
    g_free(ad);
}

/* Close picker first (avoids modal conflict), then open file dialog. */
static void
open_folder_chooser(ActionData *ad_src)
{
    ActionData *ad = g_new(ActionData, 1);
    *ad            = *ad_src;

    AdwDialog *dialog = ad->dialog;
    ad->dialog        = NULL;
    adw_dialog_close(dialog);

    GtkFileDialog *fd   = gtk_file_dialog_new();
    GFile         *home = g_file_new_for_path(g_get_home_dir());
    gtk_file_dialog_set_title(fd, "Choose working directory");
    gtk_file_dialog_set_initial_folder(fd, home);
    g_object_unref(home);
    gtk_file_dialog_select_folder(fd, ad->parent_win, NULL, on_folder_chosen, ad);
    g_object_unref(fd);
}

/* Single handler for all action buttons — works because GtkButton::clicked is unconditional. */
static void
on_action_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    ActionData *ad = data;

    if (strcmp(ad->cmd, "claude --continue") == 0) {
        /* Resume: always show session list — either for a pre-filled dir or after picking one */
        if (ad->dir[0]) {
            /* Close the picker, then show session list */
            AdwDialog *picker = ad->dialog;
            adw_dialog_close(picker);
            show_session_list(ad->dir, ad->picked, ad->picked_data, ad->parent_win);
        } else {
            open_folder_chooser(ad);
        }
    } else if (ad->dir[0]) {
        fire_and_close(ad);
    } else {
        open_folder_chooser(ad);
    }
}

static gboolean
filter_row(GtkListBoxRow *row, gpointer data)
{
    const char *q = gtk_editable_get_text(GTK_EDITABLE(data));
    if (!q || !*q)
        return TRUE;

    const char *text = g_object_get_data(G_OBJECT(row), "search-text");
    if (!text)
        return TRUE;

    char    *qf    = g_utf8_casefold(q, -1);
    char    *tf    = g_utf8_casefold(text, -1);
    gboolean match = !!strstr(tf, qf);
    g_free(qf);
    g_free(tf);
    return match;
}

static GtkWidget *
make_section_label(const char *text)
{
    GtkWidget *lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_add_css_class(lbl, "heading");
    gtk_widget_set_margin_top(lbl, 12);
    gtk_widget_set_margin_bottom(lbl, 4);
    return lbl;
}

static GtkWidget *
make_listbox(void)
{
    GtkWidget *lb = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_BROWSE);
    gtk_widget_add_css_class(lb, "boxed-list");
    return lb;
}

static void
on_row_activated(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
    (void)lb;
    (void)data;
    GtkWidget *btn = g_object_get_data(G_OBJECT(row), "main-btn");
    if (btn)
        gtk_widget_activate(btn);
}

static gboolean
activate_first_visible(GtkListBox *lb)
{
    if (!lb)
        return FALSE;
    for (int i = 0;; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, i);
        if (!row)
            return FALSE;
        if (gtk_widget_get_child_visible(GTK_WIDGET(row))) {
            GtkWidget *btn = g_object_get_data(G_OBJECT(row), "main-btn");
            if (btn) {
                gtk_widget_activate(btn);
                return TRUE;
            }
        }
    }
}

typedef struct {
    GtkListBox *lb1, *lb2;
} SearchCtx;

static void
on_search_activate(GtkSearchEntry *e, gpointer data)
{
    (void)e;
    SearchCtx *ctx = data;
    if (!activate_first_visible(ctx->lb1))
        activate_first_visible(ctx->lb2);
}

/* Build a flat button with icon + title + optional subtitle + chevron. */
static GtkWidget *
make_content_button(const char *icon, const char *title, const char *subtitle)
{
    GtkWidget *btn  = gtk_button_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_hexpand(btn, TRUE);
    gtk_widget_set_margin_start(hbox, 6);
    gtk_widget_set_margin_end(hbox, 6);
    gtk_widget_set_margin_top(hbox, 8);
    gtk_widget_set_margin_bottom(hbox, 8);

    GtkWidget *img = gtk_image_new_from_icon_name(icon);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 32);
    gtk_box_append(GTK_BOX(hbox), img);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0f);
    gtk_box_append(GTK_BOX(vbox), title_lbl);

    if (subtitle && *subtitle) {
        GtkWidget *sub = gtk_label_new(subtitle);
        gtk_label_set_xalign(GTK_LABEL(sub), 0.0f);
        gtk_widget_add_css_class(sub, "caption");
        gtk_widget_add_css_class(sub, "dim-label");
        gtk_box_append(GTK_BOX(vbox), sub);
    }
    gtk_box_append(GTK_BOX(hbox), vbox);

    GtkWidget *chevron = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(chevron, "dim-label");
    gtk_box_append(GTK_BOX(hbox), chevron);

    gtk_button_set_child(GTK_BUTTON(btn), hbox);
    return btn;
}

/* Fixed action row: one flat button spanning the whole row. */
static GtkWidget *
make_action_row(const char *icon, const char *title, const char *subtitle, ActionData *ad)
{
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);

    GtkWidget *btn = make_content_button(icon, title, subtitle);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_action_clicked), ad, (GClosureNotify)g_free,
                          0);
    g_object_set_data(G_OBJECT(row), "main-btn", btn);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), btn);

    char search[640];
    g_snprintf(search, sizeof(search), "%s %s", title, subtitle ? subtitle : "");
    g_object_set_data_full(G_OBJECT(row), "search-text", g_strdup(search), g_free);
    return row;
}

/* Recent-dir row: main flat button (new claude) + resume + shell buttons. */
static GtkWidget *
make_recent_row(const char *path, const ActionData *tmpl)
{
    const char *base = strrchr(path, '/');
    const char *name = (base && base[1]) ? base + 1 : path;

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget *main_btn = make_content_button("folder-symbolic", name, path);
    /* remove the chevron from main_btn for recent rows — the suffix buttons replace it */

    ActionData *ad_main = g_new(ActionData, 1);
    *ad_main            = *tmpl;
    g_strlcpy(ad_main->dir, path, sizeof(ad_main->dir));
    char *latest = latest_claude_session(path);
    if (latest) {
        g_snprintf(ad_main->cmd, sizeof(ad_main->cmd), "claude --resume %s", latest);
        g_free(latest);
    } else {
        g_strlcpy(ad_main->cmd, "claude", sizeof(ad_main->cmd));
    }
    g_signal_connect_data(main_btn, "clicked", G_CALLBACK(on_action_clicked), ad_main,
                          (GClosureNotify)g_free, 0);
    g_object_set_data(G_OBJECT(row), "main-btn", main_btn);
    gtk_box_append(GTK_BOX(outer), main_btn);

    static const struct {
        const char *icon, *tip, *cmd;
    } btns[] = {
        { "starred-symbolic", "New Claude session", "claude" },
        { "document-open-recent-symbolic", "Resume Claude session", "claude --continue" },
        { "utilities-terminal-symbolic", "Open Shell", "" },
    };
    for (int i = 0; i < (int)(sizeof(btns) / sizeof(*btns)); i++) {
        ActionData *ad = g_new(ActionData, 1);
        *ad            = *tmpl;
        g_strlcpy(ad->cmd, btns[i].cmd, sizeof(ad->cmd));
        g_strlcpy(ad->dir, path, sizeof(ad->dir));

        GtkWidget *btn = gtk_button_new_from_icon_name(btns[i].icon);
        gtk_widget_add_css_class(btn, "flat");
        gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(btn, btns[i].tip);
        g_signal_connect_data(btn, "clicked", G_CALLBACK(on_action_clicked), ad,
                              (GClosureNotify)g_free, 0);
        gtk_box_append(GTK_BOX(outer), btn);
    }

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), outer);

    char search[1024];
    g_snprintf(search, sizeof(search), "%s %s", name, path);
    g_object_set_data_full(G_OBJECT(row), "search-text", g_strdup(search), g_free);
    return row;
}

void
session_picker_show(GtkWidget *parent_win, SessionPickedFn picked, gpointer data,
                    const char *initial_dir)
{
    AdwDialog *dialog = ADW_DIALOG(adw_dialog_new());
    adw_dialog_set_title(dialog, "New Session");
    adw_dialog_set_content_width(dialog, 540);
    adw_dialog_set_content_height(dialog, 460);

    ActionData tmpl = {
        .dialog      = dialog,
        .parent_win  = GTK_WINDOW(parent_win),
        .picked      = picked,
        .picked_data = data,
    };
    tmpl.cmd[0] = '\0';
    tmpl.dir[0] = '\0';

    GtkWidget *toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());

    GtkWidget *page   = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), "Search…");
    gtk_widget_set_margin_start(search, 16);
    gtk_widget_set_margin_end(search, 16);
    gtk_widget_set_margin_top(search, 10);
    gtk_widget_set_margin_bottom(search, 6);
    gtk_box_append(GTK_BOX(page), search);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(inner, 16);
    gtk_widget_set_margin_end(inner, 16);
    gtk_widget_set_margin_bottom(inner, 16);

    char **recents = recents_list();

    gtk_box_append(GTK_BOX(inner), make_section_label("Open"));
    GtkWidget *action_lb = make_listbox();

    static const struct {
        const char *icon, *title, *subtitle, *cmd;
    } fixed[] = {
        { "starred-symbolic", "New Claude Code session", "Start a fresh AI coding session",
          "claude" },
        { "document-open-recent-symbolic", "Resume Claude session",
          "Continue the most recent conversation", "claude --continue" },
        { "utilities-terminal-symbolic", "Open Shell", "Open a terminal shell", NULL },
    };
    for (int i = 0; i < 3; i++) {
        ActionData *ad = g_new(ActionData, 1);
        *ad            = tmpl;
        g_strlcpy(ad->cmd, fixed[i].cmd ? fixed[i].cmd : "", sizeof(ad->cmd));

        /* ponytail: only Open Shell (i==2) inherits the current session's dir;
           new/resume Claude rows always prompt so the user can pick a project. */
        if (i == 2 && initial_dir && *initial_dir)
            g_strlcpy(ad->dir, initial_dir, sizeof(ad->dir));
        else
            ad->dir[0] = '\0';

        gtk_list_box_append(GTK_LIST_BOX(action_lb),
                            make_action_row(fixed[i].icon, fixed[i].title, fixed[i].subtitle, ad));
    }
    gtk_list_box_set_filter_func(GTK_LIST_BOX(action_lb), filter_row, search, NULL);
    g_signal_connect_swapped(search, "search-changed", G_CALLBACK(gtk_list_box_invalidate_filter),
                             action_lb);
    g_signal_connect(action_lb, "row-activated", G_CALLBACK(on_row_activated), NULL);
    gtk_box_append(GTK_BOX(inner), action_lb);

    SearchCtx *sctx = g_new0(SearchCtx, 1);
    sctx->lb1       = GTK_LIST_BOX(action_lb);

    if (recents && recents[0]) {
        gtk_box_append(GTK_BOX(inner), make_section_label("Recent Directories"));
        GtkWidget *recent_lb = make_listbox();
        for (int i = 0; recents[i]; i++)
            gtk_list_box_append(GTK_LIST_BOX(recent_lb), make_recent_row(recents[i], &tmpl));
        gtk_list_box_set_filter_func(GTK_LIST_BOX(recent_lb), filter_row, search, NULL);
        g_signal_connect_swapped(search, "search-changed",
                                 G_CALLBACK(gtk_list_box_invalidate_filter), recent_lb);
        g_signal_connect(recent_lb, "row-activated", G_CALLBACK(on_row_activated), NULL);
        gtk_box_append(GTK_BOX(inner), recent_lb);
        sctx->lb2 = GTK_LIST_BOX(recent_lb);
    }
    g_strfreev(recents);
    g_signal_connect_data(search, "activate", G_CALLBACK(on_search_activate), sctx,
                          (GClosureNotify)g_free, 0);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), inner);
    gtk_box_append(GTK_BOX(page), scroll);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), page);
    adw_dialog_set_child(dialog, toolbar);
    adw_dialog_present(dialog, parent_win);
}

void
session_resume_show(GtkWidget *parent_win, SessionPickedFn picked, gpointer data, const char *dir)
{
    show_session_list(dir, picked, data, GTK_WINDOW(parent_win));
}
