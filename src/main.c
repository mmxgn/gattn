#include <adwaita.h>
#include "session.h"
#include "state_detector.h"
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

static gboolean on_toggle_grid(GtkWidget *widget, GVariant *args, gpointer data)
{
    (void)widget; (void)args; (void)data;
    grid_toggle(app.outer_stack, app.split, &sessions);
    return TRUE;
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

    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW(
        adw_application_window_new(GTK_APPLICATION(app_obj)));
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 700);
    adw_application_window_set_content(win, app.outer_stack);

    /* Ctrl+G: toggle grid view */
    GtkEventController *ctrl = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(ctrl),
                                      GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_shortcut_controller_add_shortcut(
        GTK_SHORTCUT_CONTROLLER(ctrl),
        gtk_shortcut_new(
            gtk_keyval_trigger_new(GDK_KEY_g, GDK_CONTROL_MASK),
            gtk_callback_action_new(on_toggle_grid, NULL, NULL)));
    gtk_widget_add_controller(GTK_WIDGET(win), ctrl);

    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char *argv[])
{
    session_list_init(&sessions);
    AdwApplication *a = adw_application_new("org.mmxgn.gattn",
                                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(a, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(a), argc, argv);
    g_object_unref(a);
    return status;
}
