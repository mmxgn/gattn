#include "sidebar.h"
#include <adwaita.h>

typedef struct { SidebarNewFn fn; gpointer data; } NewBtnCtx;

static void on_row_selected(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
    (void)lb;
    if (!row) return;
    Session *s = g_object_get_data(G_OBJECT(row), "gattn-session");
    if (!s) return;
    char name[32];
    g_snprintf(name, sizeof(name), "session-%d", s->id);
    gtk_stack_set_visible_child_name(GTK_STACK(data), name);
}

static GtkWidget *make_row(Session *s)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    GtkWidget *dot = gtk_label_new("●");
    gtk_widget_add_css_class(dot, "dot-idle");
    s->dot = dot;

    GtkWidget *label = gtk_label_new(s->name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), dot);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "gattn-session", s);
    return row;
}

void sidebar_add_session(GtkWidget *split, Session *s)
{
    GtkListBox *lb    = g_object_get_data(G_OBJECT(split), "gattn-listbox");
    GtkStack   *stack = g_object_get_data(G_OBJECT(split), "gattn-stack");

    GtkWidget *row = make_row(s);
    gtk_list_box_append(lb, row);

    char name[32];
    g_snprintf(name, sizeof(name), "session-%d", s->id);
    gtk_stack_add_named(stack, s->terminal, name);

    if (!gtk_list_box_get_selected_row(lb))
        gtk_list_box_select_row(lb, GTK_LIST_BOX_ROW(row));
}

static void on_new_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    GtkWidget  *split = data;
    NewBtnCtx  *ctx   = g_object_get_data(G_OBJECT(split), "gattn-new-ctx");
    if (ctx && ctx->fn) ctx->fn(split, ctx->data);
}

GtkWidget *sidebar_new(SessionList *sessions, SidebarNewFn on_new, gpointer on_new_data)
{
    GtkWidget *stack = gtk_stack_new();

    /* content side */
    GtkWidget *content_toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content_toolbar),
                                 adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content_toolbar), stack);
    AdwNavigationPage *content_page = adw_navigation_page_new(content_toolbar, "Terminal");

    /* sidebar: scrolled listbox */
    GtkWidget *lb = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_SINGLE);
    g_signal_connect(lb, "row-selected", G_CALLBACK(on_row_selected), stack);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), lb);

    GtkWidget *sidebar_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(sidebar_header),
                                    gtk_label_new("gattn"));

    GtkWidget *sidebar_toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sidebar_toolbar), sidebar_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(sidebar_toolbar), scroll);
    AdwNavigationPage *sidebar_page = adw_navigation_page_new(sidebar_toolbar, "Sessions");

    /* split view — created before button so we can pass it as signal data */
    GtkWidget *split = adw_navigation_split_view_new();
    adw_navigation_split_view_set_sidebar(ADW_NAVIGATION_SPLIT_VIEW(split),
                                          ADW_NAVIGATION_PAGE(sidebar_page));
    adw_navigation_split_view_set_content(ADW_NAVIGATION_SPLIT_VIEW(split),
                                          ADW_NAVIGATION_PAGE(content_page));

    g_object_set_data(G_OBJECT(split), "gattn-listbox", lb);
    g_object_set_data(G_OBJECT(split), "gattn-stack",   stack);

    /* "+" button */
    NewBtnCtx *ctx = g_new(NewBtnCtx, 1);
    ctx->fn   = on_new;
    ctx->data = on_new_data;
    g_object_set_data_full(G_OBJECT(split), "gattn-new-ctx", ctx, g_free);

    GtkWidget *btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(btn, "New session");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_new_clicked), split);
    adw_header_bar_pack_end(ADW_HEADER_BAR(sidebar_header), btn);

    for (int i = 0; i < sessions->count; i++)
        sidebar_add_session(split, &sessions->items[i]);

    return split;
}
