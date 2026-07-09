#include <adwaita.h>
#include "session.h"
#include "state_detector.h"

static SessionList sessions;

static void on_activate(AdwApplication *app, gpointer data)
{
    (void)data;
    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW(
        adw_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(GTK_WINDOW(win), "gattn");
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 700);

    Session *s = session_create(&sessions, "shell");
    session_spawn(s, NULL);
    state_detector_start(s);
    adw_application_window_set_content(win, s->terminal);

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
