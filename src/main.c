#include <adwaita.h>
#include "session.h"
#include "state_detector.h"
#include "ui/sidebar.h"

static SessionList sessions;

static const char CSS[] =
    ".dot-idle        { color: gray;    }"
    ".dot-working     { color: #4CAF50; }"
    ".dot-needs-input { color: #FFC107; }"
    ".dot-done        { color: #2196F3; }";

static void on_activate(AdwApplication *app, gpointer data)
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

    GtkWidget *split = sidebar_new(&sessions);

    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW(
        adw_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 700);
    adw_application_window_set_content(win, split);
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char *argv[])
{
    session_list_init(&sessions);
    AdwApplication *app = adw_application_new("org.mmxgn.gattn",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
