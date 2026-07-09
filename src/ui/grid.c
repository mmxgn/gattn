#include "grid.h"
#include <adwaita.h>
#include <string.h>

/*
 * Enter: build empty frames → switch outer_stack to "grid" → THEN fill frames.
 * VTE's first allocation is at tile-size so there is no resize and no stale frame.
 *
 * Exit: restore terminals to inner_stack while grid is still showing → THEN
 * switch back to "split".  All steps are synchronous within one main-loop tick
 * so GTK batches redraws and only the final state is painted.
 */

static void grid_enter(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions,
                       GCallback back_cb, gpointer back_data)
{
    GtkWidget *inner_stack = g_object_get_data(G_OBJECT(split), "gattn-stack");

    /* 1. Build grid with empty frames */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_widget_set_margin_start(grid, 4);
    gtk_widget_set_margin_end(grid, 4);
    gtk_widget_set_margin_top(grid, 4);
    gtk_widget_set_margin_bottom(grid, 4);

    /* count only top-level sessions — children share parent's terminal */
    int tile_count = 0;
    for (int i = 0; i < sessions->count; i++)
        if (sessions->items[i].parent_id == 0) tile_count++;

    GtkWidget **frames = g_new(GtkWidget *, tile_count ? tile_count : 1);
    int tile = 0;
    for (int i = 0; i < sessions->count; i++) {
        Session *s = &sessions->items[i];
        if (s->parent_id != 0) continue;
        frames[tile] = gtk_frame_new(s->name);
        gtk_widget_set_hexpand(frames[tile], TRUE);
        gtk_widget_set_vexpand(frames[tile], TRUE);
        gtk_grid_attach(GTK_GRID(grid), frames[tile], tile % 2, tile / 2, 1, 1);
        tile++;
    }

    GtkWidget *hdr  = adw_header_bar_new();
    GtkWidget *back = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_tooltip_text(back, "Split view (Escape)");
    g_signal_connect(back, "clicked", back_cb, back_data);
    adw_header_bar_pack_start(ADW_HEADER_BAR(hdr), back);

    GtkWidget *wrapper = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(wrapper), hdr);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(wrapper), grid);

    /* 2. Switch now — split is hidden before we touch inner_stack */
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid",         grid);
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid-wrapper", wrapper);
    gtk_stack_add_named(GTK_STACK(outer_stack), wrapper, "grid");
    gtk_stack_set_visible_child_name(GTK_STACK(outer_stack), "grid");

    /* 3. Fill frames — VTE is new to these tiles, no resize */
    tile = 0;
    for (int i = 0; i < sessions->count; i++) {
        Session *s = &sessions->items[i];
        if (s->parent_id != 0) continue;
        g_object_ref(s->terminal);
        gtk_stack_remove(GTK_STACK(inner_stack), s->terminal);
        gtk_frame_set_child(GTK_FRAME(frames[tile]), s->terminal);
        g_object_unref(s->terminal);
        tile++;
    }
    g_free(frames);
}

static void grid_exit(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions)
{
    GtkWidget *inner_stack = g_object_get_data(G_OBJECT(split), "gattn-stack");
    GtkWidget *wrapper     = g_object_get_data(G_OBJECT(outer_stack), "gattn-grid-wrapper");

    /* 1. Restore terminals while grid is still showing — inner_stack is hidden */
    for (int i = 0; i < sessions->count; i++) {
        Session *s = &sessions->items[i];
        if (s->parent_id != 0) continue; /* children share parent terminal, not in grid */
        GtkWidget *frame = gtk_widget_get_parent(s->terminal);

        g_object_ref(s->terminal);
        gtk_frame_set_child(GTK_FRAME(frame), NULL);

        char name[32];
        g_snprintf(name, sizeof(name), "session-%d", s->id);
        gtk_stack_add_named(GTK_STACK(inner_stack), s->terminal, name);
        g_object_unref(s->terminal);
    }

    GtkListBox    *lb  = g_object_get_data(G_OBJECT(split), "gattn-listbox");
    GtkListBoxRow *sel = gtk_list_box_get_selected_row(lb);
    if (sel) {
        Session *s = g_object_get_data(G_OBJECT(sel), "gattn-session");
        if (s) {
            int display_id = (s->parent_id != 0) ? s->parent_id : s->id;
            char name[32];
            g_snprintf(name, sizeof(name), "session-%d", display_id);
            gtk_stack_set_visible_child_name(GTK_STACK(inner_stack), name);
        }
    }

    /* 2. Switch back — split already has its terminals */
    gtk_stack_set_visible_child_name(GTK_STACK(outer_stack), "split");
    if (wrapper) gtk_stack_remove(GTK_STACK(outer_stack), wrapper);
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid",         NULL);
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid-wrapper", NULL);
}

void grid_toggle(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions,
                 GCallback back_cb, gpointer back_data)
{
    const char *cur = gtk_stack_get_visible_child_name(GTK_STACK(outer_stack));
    if (!cur || strcmp(cur, "split") == 0)
        grid_enter(outer_stack, split, sessions, back_cb, back_data);
    else
        grid_exit(outer_stack, split, sessions);
}
