#include "grid.h"
#include <adwaita.h>
#include <string.h>

static void grid_enter(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions)
{
    GtkWidget *inner_stack = g_object_get_data(G_OBJECT(split), "gattn-stack");

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_widget_set_margin_start(grid, 4);
    gtk_widget_set_margin_end(grid, 4);
    gtk_widget_set_margin_top(grid, 4);
    gtk_widget_set_margin_bottom(grid, 4);

    for (int i = 0; i < sessions->count; i++) {
        Session *s = &sessions->items[i];

        /* steal terminal from inner stack */
        g_object_ref(s->terminal);
        gtk_stack_remove(GTK_STACK(inner_stack), s->terminal);

        GtkWidget *frame = gtk_frame_new(s->name);
        gtk_frame_set_child(GTK_FRAME(frame), s->terminal);
        if (s->state == SESSION_NEEDS_INPUT)
            gtk_widget_add_css_class(frame, "tile-needs-input");
        gtk_widget_set_hexpand(frame, TRUE);
        gtk_widget_set_vexpand(frame, TRUE);
        gtk_grid_attach(GTK_GRID(grid), frame, i % 2, i / 2, 1, 1);
        g_object_unref(s->terminal);
    }

    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid", grid);
    gtk_stack_add_named(GTK_STACK(outer_stack), grid, "grid");
    gtk_stack_set_visible_child_name(GTK_STACK(outer_stack), "grid");
}

static void grid_exit(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions)
{
    GtkWidget *inner_stack = g_object_get_data(G_OBJECT(split), "gattn-stack");
    GtkWidget *grid        = g_object_get_data(G_OBJECT(outer_stack), "gattn-grid");

    for (int i = 0; i < sessions->count; i++) {
        Session  *s     = &sessions->items[i];
        GtkWidget *frame = gtk_widget_get_parent(s->terminal);

        /* return terminal to inner stack */
        g_object_ref(s->terminal);
        gtk_frame_set_child(GTK_FRAME(frame), NULL);

        char name[32];
        g_snprintf(name, sizeof(name), "session-%d", s->id);
        gtk_stack_add_named(GTK_STACK(inner_stack), s->terminal, name);
        g_object_unref(s->terminal);
    }

    /* restore the previously selected terminal as visible */
    GtkListBox    *lb  = g_object_get_data(G_OBJECT(split), "gattn-listbox");
    GtkListBoxRow *sel = gtk_list_box_get_selected_row(lb);
    if (sel) {
        Session *s = g_object_get_data(G_OBJECT(sel), "gattn-session");
        if (s) {
            char name[32];
            g_snprintf(name, sizeof(name), "session-%d", s->id);
            gtk_stack_set_visible_child_name(GTK_STACK(inner_stack), name);
        }
    }

    gtk_stack_set_visible_child_name(GTK_STACK(outer_stack), "split");
    gtk_stack_remove(GTK_STACK(outer_stack), grid);
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid", NULL);
}

void grid_toggle(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions)
{
    const char *cur = gtk_stack_get_visible_child_name(GTK_STACK(outer_stack));
    if (!cur || strcmp(cur, "split") == 0)
        grid_enter(outer_stack, split, sessions);
    else
        grid_exit(outer_stack, split, sessions);
}
