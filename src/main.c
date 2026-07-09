#include <adwaita.h>
#include "session.h"
#include "state_detector.h"
#include "notify.h"
#include "ui/sidebar.h"
#include "ui/grid.h"

static SessionList sessions;

static const char CSS[] =
    ".dot-idle        { color: gray;    }"
    ".dot-working     { color: #4CAF50; }"
    ".dot-needs-input { color: #FFC107; }"
    ".dot-done        { color: #2196F3; }"
    "@keyframes needs-input-pulse {"
    "  0%   { opacity: 1.0; }"
    "  50%  { opacity: 0.35; }"
    "  100% { opacity: 1.0; }"
    "}"
    ".tile-needs-input { animation: needs-input-pulse 1s ease-in-out infinite; }";

static struct {
    GtkWidget *outer_stack;
    GtkWidget *split;
} app;

static void on_toggle_grid(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param; (void)data;
    grid_toggle(app.outer_stack, app.split, &sessions);
}

static void on_raise(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(data));
    if (win) gtk_window_present(win);
}

static void on_activate(AdwApplication *app_obj, gpointer data)
{
    (void)data;

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, CSS);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    Session *s = session_create(&sessions, "shell");
    session_spawn(s, NULL);
    state_detector_start(s);

    app.split = sidebar_new(&sessions);

    app.outer_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app.outer_stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(app.outer_stack), app.split, "split");

    GtkWidget *toast_overlay = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toast_overlay), app.outer_stack);

    notify_watch(s, ADW_TOAST_OVERLAY(toast_overlay), G_APPLICATION(app_obj));

    /* register actions */
    GSimpleAction *act = g_simple_action_new("toggle-grid", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(on_toggle_grid), NULL);
    g_action_map_add_action(G_ACTION_MAP(app_obj), G_ACTION(act));
    g_object_unref(act);

    GSimpleAction *raise_act = g_simple_action_new("raise", NULL);
    g_signal_connect(raise_act, "activate", G_CALLBACK(on_raise), app_obj);
    g_action_map_add_action(G_ACTION_MAP(app_obj), G_ACTION(raise_act));
    g_object_unref(raise_act);

    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW(
        adw_application_window_new(GTK_APPLICATION(app_obj)));
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 700);
    adw_application_window_set_content(win, toast_overlay);

    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char *argv[])
{
    session_list_init(&sessions);
    AdwApplication *a = adw_application_new("org.mmxgn.gattn",
                                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(a, "activate", G_CALLBACK(on_activate), NULL);

    /* bind Ctrl+G to the action — must be set before g_application_run */
    const char *accels[] = { "<Control>g", NULL };
    gtk_application_set_accels_for_action(GTK_APPLICATION(a),
                                          "app.toggle-grid", accels);

    int status = g_application_run(G_APPLICATION(a), argc, argv);
    g_object_unref(a);
    return status;
}
