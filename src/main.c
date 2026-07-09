#include <adwaita.h>

static void on_activate(AdwApplication *app, gpointer data)
{
    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW(
        adw_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(GTK_WINDOW(win), "gattn");
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 700);
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char *argv[])
{
    AdwApplication *app = adw_application_new("org.mmxgn.gattn",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
