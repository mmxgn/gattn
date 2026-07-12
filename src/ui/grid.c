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

typedef struct {
    Session **tiles;
    int       count;
    int       cols;
} GridNav;

typedef struct {
    GridNav *gn;
    int      dx, dy;
} NavCtx;

static void
grid_nav_free(GridNav *gn)
{
    g_free(gn->tiles);
    g_free(gn);
}

static void
on_grid_nav(GSimpleAction *a, GVariant *p, gpointer data)
{
    (void)a;
    (void)p;
    NavCtx  *nc = data;
    GridNav *gn = nc->gn;

    int cur = -1;
    for (int i = 0; i < gn->count; i++) {
        if (gtk_widget_is_focus(gn->tiles[i]->terminal)) {
            cur = i;
            break;
        }
    }
    if (cur < 0)
        cur = 0;

    int row     = cur / gn->cols;
    int col     = cur % gn->cols;
    int new_row = row + nc->dy;
    int new_col = col + nc->dx;
    int max_row = (gn->count - 1) / gn->cols;
    int max_col = MIN(gn->cols - 1,
                      gn->count - 1 - max_row * gn->cols + (new_row < max_row ? gn->cols - 1 : 0));

    (void)max_col;
    if (new_col < 0 || new_col >= gn->cols)
        return;
    if (new_row < 0 || new_row > max_row)
        return;

    int next = new_row * gn->cols + new_col;
    if (next >= 0 && next < gn->count)
        gtk_widget_grab_focus(gn->tiles[next]->terminal);
}

static GtkWidget *
make_grid_statusbar(void)
{
    static const struct {
        const char *key;
        const char *label;
    } hints[] = {
        { "M-h/l", "Left/Right" },
        { "M-k/j", "Up/Down" },
        { "^G", "Exit grid" },
    };

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_start(bar, 10);
    gtk_widget_set_margin_end(bar, 10);
    gtk_widget_set_margin_top(bar, 4);
    gtk_widget_set_margin_bottom(bar, 4);

    for (gsize i = 0; i < sizeof(hints) / sizeof(*hints); i++) {
        GtkWidget *pair = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *k    = gtk_label_new(hints[i].key);
        gtk_widget_add_css_class(k, "caption");
        gtk_widget_add_css_class(k, "monospace");
        GtkWidget *l = gtk_label_new(hints[i].label);
        gtk_widget_add_css_class(l, "caption");
        gtk_widget_add_css_class(l, "dim-label");
        gtk_box_append(GTK_BOX(pair), k);
        gtk_box_append(GTK_BOX(pair), l);
        gtk_box_append(GTK_BOX(bar), pair);
    }
    return bar;
}

static void
grid_enter(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions, GCallback back_cb,
           gpointer back_data)
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
        if (sessions->items[i] && sessions->items[i]->parent_id == 0)
            tile_count++;

    GtkWidget **frames = g_new(GtkWidget *, tile_count ? tile_count : 1);
    GridNav    *gn     = g_new0(GridNav, 1);
    gn->tiles          = g_new(Session *, tile_count ? tile_count : 1);
    gn->cols           = 2;

    int tile = 0;
    for (int i = 0; i < sessions->count; i++) {
        Session *s = sessions->items[i];
        if (!s || s->parent_id != 0)
            continue;
        frames[tile]    = gtk_frame_new(s->name);
        gn->tiles[tile] = s;
        char tile_class[12];
        g_snprintf(tile_class, sizeof(tile_class), "tile-%d", tile % 6);
        gtk_widget_add_css_class(frames[tile], tile_class);
        gtk_widget_set_hexpand(frames[tile], TRUE);
        gtk_widget_set_vexpand(frames[tile], TRUE);
        gtk_grid_attach(GTK_GRID(grid), frames[tile], tile % 2, tile / 2, 1, 1);
        tile++;
    }
    gn->count = tile;

    GtkWidget *hdr  = adw_header_bar_new();
    GtkWidget *back = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_tooltip_text(back, "Split view (Ctrl+G)");
    g_signal_connect(back, "clicked", back_cb, back_data);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), back);

    GtkWidget *wrapper = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(wrapper), hdr);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(wrapper), grid);
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(wrapper), make_grid_statusbar());

    /* navigation shortcuts */
    GSimpleActionGroup *ag = g_simple_action_group_new();

    static const struct {
        const char *name;
        int         dx, dy;
    } dirs[] = {
        { "nav-left", -1, 0 },
        { "nav-right", 1, 0 },
        { "nav-up", 0, -1 },
        { "nav-down", 0, 1 },
    };
    for (gsize i = 0; i < sizeof(dirs) / sizeof(*dirs); i++) {
        NavCtx *nc         = g_new(NavCtx, 1);
        nc->gn             = gn;
        nc->dx             = dirs[i].dx;
        nc->dy             = dirs[i].dy;
        GSimpleAction *act = g_simple_action_new(dirs[i].name, NULL);
        g_signal_connect_data(act, "activate", G_CALLBACK(on_grid_nav), nc, (GClosureNotify)g_free,
                              0);
        g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act));
        g_object_unref(act);
    }
    gtk_widget_insert_action_group(wrapper, "gn", G_ACTION_GROUP(ag));
    g_object_unref(ag);

    GtkShortcutController *sc = GTK_SHORTCUT_CONTROLLER(gtk_shortcut_controller_new());
    gtk_shortcut_controller_set_scope(sc, GTK_SHORTCUT_SCOPE_MANAGED);

    static const struct {
        guint           key;
        GdkModifierType mod;
        const char     *action;
    } cuts[] = {
        { GDK_KEY_Left, GDK_ALT_MASK, "gn.nav-left" },
        { GDK_KEY_h, GDK_ALT_MASK, "gn.nav-left" },
        { GDK_KEY_Right, GDK_ALT_MASK, "gn.nav-right" },
        { GDK_KEY_l, GDK_ALT_MASK, "gn.nav-right" },
        { GDK_KEY_Up, GDK_ALT_MASK, "gn.nav-up" },
        { GDK_KEY_k, GDK_ALT_MASK, "gn.nav-up" },
        { GDK_KEY_Down, GDK_ALT_MASK, "gn.nav-down" },
        { GDK_KEY_j, GDK_ALT_MASK, "gn.nav-down" },
    };
    for (gsize i = 0; i < sizeof(cuts) / sizeof(*cuts); i++)
        gtk_shortcut_controller_add_shortcut(
            sc, gtk_shortcut_new(gtk_keyval_trigger_new(cuts[i].key, cuts[i].mod),
                                 gtk_named_action_new(cuts[i].action)));
    gtk_widget_add_controller(wrapper, GTK_EVENT_CONTROLLER(sc));

    /* store nav so it lives as long as the wrapper */
    g_object_set_data_full(G_OBJECT(wrapper), "gattn-grid-nav", gn, (GDestroyNotify)grid_nav_free);

    /* 2. Switch now — split is hidden before we touch inner_stack */
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid", grid);
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid-wrapper", wrapper);
    gtk_stack_add_named(GTK_STACK(outer_stack), wrapper, "grid");
    gtk_stack_set_visible_child_name(GTK_STACK(outer_stack), "grid");

    /* 3. Fill frames — VTE is new to these tiles, no resize */
    tile = 0;
    for (int i = 0; i < sessions->count; i++) {
        Session *s = sessions->items[i];
        if (!s || s->parent_id != 0)
            continue;
        g_object_ref(s->terminal);
        gtk_stack_remove(GTK_STACK(inner_stack), s->terminal);
        gtk_frame_set_child(GTK_FRAME(frames[tile]), s->terminal);
        g_object_unref(s->terminal);
        tile++;
    }
    g_free(frames);
}

static void
grid_exit(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions)
{
    GtkWidget *inner_stack = g_object_get_data(G_OBJECT(split), "gattn-stack");
    GtkWidget *wrapper     = g_object_get_data(G_OBJECT(outer_stack), "gattn-grid-wrapper");

    /* 1. Restore terminals while grid is still showing — inner_stack is hidden */
    for (int i = 0; i < sessions->count; i++) {
        Session *s = sessions->items[i];
        if (!s || s->parent_id != 0)
            continue; /* children share parent terminal, not in grid */
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
            int  display_id = (s->parent_id != 0) ? s->parent_id : s->id;
            char name[32];
            g_snprintf(name, sizeof(name), "session-%d", display_id);
            gtk_stack_set_visible_child_name(GTK_STACK(inner_stack), name);
        }
    }

    /* 2. Switch back — split already has its terminals */
    gtk_stack_set_visible_child_name(GTK_STACK(outer_stack), "split");
    if (wrapper)
        gtk_stack_remove(GTK_STACK(outer_stack), wrapper);
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid", NULL);
    g_object_set_data(G_OBJECT(outer_stack), "gattn-grid-wrapper", NULL);
}

void
grid_toggle(GtkWidget *outer_stack, GtkWidget *split, SessionList *sessions, GCallback back_cb,
            gpointer back_data)
{
    const char *cur = gtk_stack_get_visible_child_name(GTK_STACK(outer_stack));
    if (!cur || strcmp(cur, "split") == 0)
        grid_enter(outer_stack, split, sessions, back_cb, back_data);
    else
        grid_exit(outer_stack, split, sessions);
}
