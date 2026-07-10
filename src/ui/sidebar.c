#define _POSIX_C_SOURCE 200809L
#include "sidebar.h"
#include <adwaita.h>
#include <gio/gio.h>
#include <signal.h>
#include <string.h>

typedef struct {
    SidebarNewFn fn;
    gpointer     data;
} NewBtnCtx;

void sidebar_rename_session(Session *s, GtkWidget *parent_widget);

/* ── diff dialog ── */

void
sidebar_show_diff(Session *s, GtkWidget *parent_widget)
{
    char cwd[512] = "";
    if (s->pid > 0) {
        char proc[64];
        g_snprintf(proc, sizeof(proc), "/proc/%d/cwd", s->pid);
        char *link = g_file_read_link(proc, NULL);
        if (link) {
            g_strlcpy(cwd, link, sizeof(cwd));
            g_free(link);
        }
    }
    if (!cwd[0] && s->cwd[0])
        g_strlcpy(cwd, s->cwd, sizeof(cwd));
    if (!cwd[0])
        return;

    const char *argv[] = { "git", "diff", "HEAD", NULL };
    char       *out    = NULL;
    g_spawn_sync(cwd, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, NULL, NULL, NULL);

    AdwDialog *dialog = ADW_DIALOG(adw_dialog_new());
    adw_dialog_set_title(dialog, cwd);
    adw_dialog_set_content_width(dialog, 860);
    adw_dialog_set_content_height(dialog, 640);

    GtkWidget *toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());

    GtkTextBuffer *tbuf = gtk_text_buffer_new(NULL);
    /* paragraph-background colours the full line width, not just the characters */
    gtk_text_buffer_create_tag(tbuf, "add", "paragraph-background", "#1c3828", "foreground",
                               "#7ee787", NULL);
    gtk_text_buffer_create_tag(tbuf, "del", "paragraph-background", "#3c1414", "foreground",
                               "#f85149", NULL);
    gtk_text_buffer_create_tag(tbuf, "hunk", "paragraph-background", "#0d2045", "foreground",
                               "#79c0ff", NULL);
    gtk_text_buffer_create_tag(tbuf, "header", "foreground", "#8b949e", "weight", PANGO_WEIGHT_BOLD,
                               NULL);

    GtkTextIter it;
    gtk_text_buffer_get_end_iter(tbuf, &it);

    if (out && *out) {
        char **lines = g_strsplit(out, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            const char *ln  = lines[i];
            const char *tag = NULL;
            if (ln[0] == '+' && ln[1] != '+')
                tag = "add";
            else if (ln[0] == '-' && ln[1] != '-')
                tag = "del";
            else if (g_str_has_prefix(ln, "@@"))
                tag = "hunk";
            else if (g_str_has_prefix(ln, "diff ") || g_str_has_prefix(ln, "index ")
                     || g_str_has_prefix(ln, "--- ") || g_str_has_prefix(ln, "+++ "))
                tag = "header";

            GtkTextIter start = it;
            char       *nl    = g_strdup_printf("%s\n", ln);
            gtk_text_buffer_insert(tbuf, &it, nl, -1);
            g_free(nl);
            if (tag)
                gtk_text_buffer_apply_tag_by_name(tbuf, tag, &start, &it);
        }
        g_strfreev(lines);
    } else {
        gtk_text_buffer_insert(tbuf, &it, "No changes (or not a git repository).", -1);
    }
    g_free(out);

    GtkWidget *tv = gtk_text_view_new_with_buffer(tbuf);
    g_object_unref(tbuf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(tv), 8);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tv);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), scroll);
    adw_dialog_set_child(dialog, toolbar);

    GtkRoot *root = gtk_widget_get_root(parent_widget);
    adw_dialog_present(dialog, root ? GTK_WIDGET(root) : parent_widget);
}

/* ── rename ── */

typedef struct {
    Session   *s;
    GtkWidget *entry;
} RenameCtx;

static void
on_rename_response(AdwAlertDialog *d, const char *resp, gpointer data)
{
    (void)d;
    RenameCtx *rc = data;
    if (g_strcmp0(resp, "rename") == 0) {
        const char *txt = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(rc->entry)));
        if (txt && *txt) {
            g_strlcpy(rc->s->name, txt, sizeof(rc->s->name));
            if (rc->s->name_label)
                gtk_label_set_text(GTK_LABEL(rc->s->name_label), txt);
        }
    }
    g_free(rc);
}

void
sidebar_rename_session(Session *s, GtkWidget *parent_widget)
{
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new("Rename session", NULL));
    adw_alert_dialog_add_responses(dlg, "cancel", "Cancel", "rename", "Rename", NULL);
    adw_alert_dialog_set_response_appearance(dlg, "rename", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dlg, "rename");
    adw_alert_dialog_set_close_response(dlg, "cancel");

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), s->name);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    adw_alert_dialog_set_extra_child(dlg, entry);

    RenameCtx *rctx = g_new(RenameCtx, 1);
    rctx->s         = s;
    rctx->entry     = entry;
    g_signal_connect(dlg, "response", G_CALLBACK(on_rename_response), rctx);

    GtkRoot *root = gtk_widget_get_root(parent_widget);
    adw_dialog_present(ADW_DIALOG(dlg), root ? GTK_WIDGET(root) : parent_widget);
    gtk_widget_grab_focus(entry);
}

/* ── right-click context menu ── */

static void
on_open_folder_clicked(GtkButton *btn, gpointer data)
{
    g_app_info_launch_default_for_uri((const char *)data, NULL, NULL);
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
}

static void
on_diff_menu_clicked(GtkButton *btn, gpointer data)
{
    Session   *s   = data;
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
    sidebar_show_diff(s, GTK_WIDGET(btn));
}

static void
on_rename_menu_clicked(GtkButton *btn, gpointer data)
{
    Session   *s   = data;
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
    sidebar_rename_session(s, GTK_WIDGET(btn));
}

static void
on_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
    (void)n_press;
    Session   *s   = data;
    GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

    const char *cwd = s->cwd[0] ? s->cwd : g_get_home_dir();
    char        uri[560];
    g_snprintf(uri, sizeof(uri), "file://%s", cwd);

    GtkWidget *folder_btn = gtk_button_new_with_label("Open Folder");
    gtk_widget_add_css_class(folder_btn, "flat");
    gtk_widget_set_halign(folder_btn, GTK_ALIGN_FILL);
    g_signal_connect_data(folder_btn, "clicked", G_CALLBACK(on_open_folder_clicked), g_strdup(uri),
                          (GClosureNotify)g_free, 0);

    GtkWidget *diff_btn = gtk_button_new_with_label("Show Diff");
    gtk_widget_add_css_class(diff_btn, "flat");
    gtk_widget_set_halign(diff_btn, GTK_ALIGN_FILL);
    g_signal_connect(diff_btn, "clicked", G_CALLBACK(on_diff_menu_clicked), s);

    GtkWidget *rename_btn = gtk_button_new_with_label("Rename");
    gtk_widget_add_css_class(rename_btn, "flat");
    gtk_widget_set_halign(rename_btn, GTK_ALIGN_FILL);
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(on_rename_menu_clicked), s);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(vbox), folder_btn);
    gtk_box_append(GTK_BOX(vbox), diff_btn);
    gtk_box_append(GTK_BOX(vbox), rename_btn);

    GtkWidget *pop = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(pop), vbox);
    gtk_widget_set_parent(pop, row);
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &rect);
    g_signal_connect_swapped(pop, "closed", G_CALLBACK(gtk_widget_unparent), pop);
    gtk_popover_popup(GTK_POPOVER(pop));

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

/* ── inline row button callbacks ── */

static void
on_folder_btn_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    Session    *s   = data;
    const char *cwd = s->cwd[0] ? s->cwd : g_get_home_dir();
    char        uri[560];
    g_snprintf(uri, sizeof(uri), "file://%s", cwd);
    g_app_info_launch_default_for_uri(uri, NULL, NULL);
}

static void
on_diff_btn_clicked(GtkButton *btn, gpointer data)
{
    sidebar_show_diff((Session *)data, GTK_WIDGET(btn));
}

static void
on_close_btn_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    Session *s = data;
    if (s->pid > 0)
        kill(s->pid, SIGHUP);
}

/* ── row builder ── */

static GtkWidget *
make_icon_btn(const char *icon, const char *tooltip, GCallback cb, gpointer data)
{
    GtkWidget *btn = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(btn, tooltip);
    g_signal_connect(btn, "clicked", cb, data);
    return btn;
}

static void
on_row_selected(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
    (void)lb;
    if (!row)
        return;
    Session *s = g_object_get_data(G_OBJECT(row), "gattn-session");
    if (!s)
        return;
    int  display_id = (s->parent_id != 0) ? s->parent_id : s->id;
    char name[32];
    g_snprintf(name, sizeof(name), "session-%d", display_id);
    gtk_stack_set_visible_child_name(GTK_STACK(data), name);
}

static GtkWidget *
make_row(Session *s)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(box, s->parent_id ? 24 : 8);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *dot = gtk_label_new("●");
    gtk_widget_add_css_class(dot, "dot-idle");
    s->dot = dot;

    char display_name[72];
    if (s->parent_id)
        g_snprintf(display_name, sizeof(display_name), "·  %s", s->name);
    else
        g_strlcpy(display_name, s->name, sizeof(display_name));
    GtkWidget *name_lbl = gtk_label_new(display_name);
    gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0f);
    s->name_label = name_lbl;

    char cwd_display[256] = "~";
    if (s->cwd[0]) {
        const char *base = strrchr(s->cwd, '/');
        g_strlcpy(cwd_display, (base && base[1]) ? base + 1 : s->cwd, sizeof(cwd_display));
    }
    GtkWidget *cwd_lbl = gtk_label_new(cwd_display);
    gtk_label_set_xalign(GTK_LABEL(cwd_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(cwd_lbl), PANGO_ELLIPSIZE_START);
    gtk_widget_add_css_class(cwd_lbl, "caption");
    gtk_widget_add_css_class(cwd_lbl, "dim-label");
    s->cwd_label = cwd_lbl;

    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_box_append(GTK_BOX(labels), name_lbl);
    gtk_box_append(GTK_BOX(labels), cwd_lbl);

    gtk_box_append(GTK_BOX(box), dot);
    gtk_box_append(GTK_BOX(box), labels);
    gtk_box_append(GTK_BOX(box), make_icon_btn("folder-open-symbolic", "Open folder",
                                               G_CALLBACK(on_folder_btn_clicked), s));
    gtk_box_append(GTK_BOX(box), make_icon_btn("document-edit-symbolic", "Show diff (Ctrl+Shift+D)",
                                               G_CALLBACK(on_diff_btn_clicked), s));
    gtk_box_append(GTK_BOX(box), make_icon_btn("window-close-symbolic", "Close session (Ctrl+W)",
                                               G_CALLBACK(on_close_btn_clicked), s));

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "gattn-session", s);

    GtkGesture *rc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), GDK_BUTTON_SECONDARY);
    g_signal_connect(rc, "pressed", G_CALLBACK(on_right_click), s);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(rc));

    return row;
}

void
sidebar_add_session(GtkWidget *split, Session *s)
{
    GtkListBox *lb    = g_object_get_data(G_OBJECT(split), "gattn-listbox");
    GtkStack   *stack = g_object_get_data(G_OBJECT(split), "gattn-stack");

    GtkWidget *row = make_row(s);
    gtk_list_box_append(lb, row);

    if (s->parent_id == 0) {
        char name[32];
        g_snprintf(name, sizeof(name), "session-%d", s->id);
        gtk_stack_add_named(stack, s->terminal, name);

        if (!gtk_list_box_get_selected_row(lb))
            gtk_list_box_select_row(lb, GTK_LIST_BOX_ROW(row));
    }
}

/* ── shortcut bar ── */

static GtkWidget *
make_shortcut_bar(void)
{
    static const struct {
        const char *key;
        const char *action;
    } hints[] = {
        { "^N", "New" },    { "^⇧W", "Close" },  { "^↓/⇥", "Next" }, { "^↑/⇧⇥", "Prev" },
        { "^PgUp", "Top" }, { "^PgDn", "Bot" }, { "^⇧A", "Unattended" },
        { "^G", "Grid" },   { "^⇧D", "Diff" },
    };
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 1);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_widget_set_margin_top(grid, 6);
    gtk_widget_set_margin_bottom(grid, 6);

    for (int i = 0; i < (int)(sizeof(hints) / sizeof(*hints)); i++) {
        GtkWidget *key = gtk_label_new(hints[i].key);
        gtk_label_set_xalign(GTK_LABEL(key), 1.0f);
        gtk_widget_add_css_class(key, "caption");
        gtk_widget_add_css_class(key, "monospace");

        GtkWidget *lbl = gtk_label_new(hints[i].action);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_add_css_class(lbl, "caption");
        gtk_widget_add_css_class(lbl, "dim-label");

        gtk_grid_attach(GTK_GRID(grid), key, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), lbl, 1, i, 1, 1);
    }
    return grid;
}

void
sidebar_remove_session(GtkWidget *split, int id)
{
    GtkListBox *lb    = g_object_get_data(G_OBJECT(split), "gattn-listbox");
    GtkStack   *stack = g_object_get_data(G_OBJECT(split), "gattn-stack");

    GtkListBoxRow *target = NULL;
    for (int i = 0;; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, i);
        if (!row)
            break;
        Session *s = g_object_get_data(G_OBJECT(row), "gattn-session");
        if (s && s->id == id) {
            target = row;
            break;
        }
    }
    if (!target)
        return;

    if (gtk_list_box_row_is_selected(target)) {
        int            idx  = gtk_list_box_row_get_index(target);
        GtkListBoxRow *next = gtk_list_box_get_row_at_index(lb, idx + 1);
        if (!next)
            next = gtk_list_box_get_row_at_index(lb, idx - 1);
        gtk_list_box_select_row(lb, next);
    }

    gtk_list_box_remove(lb, GTK_WIDGET(target));

    char name[32];
    g_snprintf(name, sizeof(name), "session-%d", id);
    GtkWidget *page = gtk_stack_get_child_by_name(GTK_STACK(stack), name);
    if (page)
        gtk_stack_remove(GTK_STACK(stack), page);
}

static void
on_new_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    GtkWidget *split = data;
    NewBtnCtx *ctx   = g_object_get_data(G_OBJECT(split), "gattn-new-ctx");
    if (ctx && ctx->fn)
        ctx->fn(split, ctx->data);
}

GtkWidget *
sidebar_new(SessionList *sessions, SidebarNewFn on_new, gpointer on_new_data)
{
    GtkWidget *stack = gtk_stack_new();

    GtkWidget *content_header  = adw_header_bar_new();
    GtkWidget *content_toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content_toolbar), content_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content_toolbar), stack);
    AdwNavigationPage *content_page = adw_navigation_page_new(content_toolbar, "Terminal");

    GtkWidget *lb = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(lb, "navigation-sidebar");
    g_signal_connect(lb, "row-selected", G_CALLBACK(on_row_selected), stack);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), lb);

    GtkWidget *sidebar_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(sidebar_header), gtk_label_new("gattn"));

    GtkWidget *sidebar_toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sidebar_toolbar), sidebar_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(sidebar_toolbar), scroll);
    gtk_widget_set_vexpand(sidebar_toolbar, TRUE);

    /* Shortcut bar outside the toolbar so it has no toolbar chrome */
    GtkWidget *sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(sidebar_box), sidebar_toolbar);
    gtk_box_append(GTK_BOX(sidebar_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(sidebar_box), make_shortcut_bar());

    AdwNavigationPage *sidebar_page = adw_navigation_page_new(sidebar_box, "Sessions");

    GtkWidget *split = adw_navigation_split_view_new();
    adw_navigation_split_view_set_sidebar(ADW_NAVIGATION_SPLIT_VIEW(split),
                                          ADW_NAVIGATION_PAGE(sidebar_page));
    adw_navigation_split_view_set_content(ADW_NAVIGATION_SPLIT_VIEW(split),
                                          ADW_NAVIGATION_PAGE(content_page));

    g_object_set_data(G_OBJECT(split), "gattn-listbox", lb);
    g_object_set_data(G_OBJECT(split), "gattn-stack", stack);
    g_object_set_data(G_OBJECT(split), "gattn-content-header", content_header);

    NewBtnCtx *ctx = g_new(NewBtnCtx, 1);
    ctx->fn        = on_new;
    ctx->data      = on_new_data;
    g_object_set_data_full(G_OBJECT(split), "gattn-new-ctx", ctx, g_free);

    GtkWidget *btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(btn, "New session");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_new_clicked), split);
    adw_header_bar_pack_end(ADW_HEADER_BAR(sidebar_header), btn);

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Preferences", "app.preferences");
    g_menu_append(menu, "About gattn", "app.about");
    GtkWidget *hamburger = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(hamburger), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(hamburger), G_MENU_MODEL(menu));
    g_object_unref(menu);
    adw_header_bar_pack_start(ADW_HEADER_BAR(sidebar_header), hamburger);

    for (int i = 0; i < sessions->count; i++)
        sidebar_add_session(split, &sessions->items[i]);

    return split;
}
