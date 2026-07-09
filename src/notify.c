#include "notify.h"
#include <gio/gio.h>

typedef struct {
    AdwToastOverlay *overlay;
    GApplication    *gapp;
} NotifyCtx;

static void
dbus_notify(const char *body)
{
    GError          *err  = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) {
        g_clear_error(&err);
        return;
    }

    GVariantBuilder actions, hints;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));

    g_dbus_connection_call_sync(
        conn, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify",
        g_variant_new("(susssasa{sv}i)", "gattn", (guint32)0, "utilities-terminal", "gattn", body,
                      &actions, &hints, 3000),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

    g_clear_error(&err);
    g_object_unref(conn);
}

static void
on_state_changed(Session *s, gpointer data)
{
    if (s->state != SESSION_NEEDS_INPUT)
        return;
    if (strcmp(s->name, "claude") != 0)
        return; /* color dot handles shell attention */

    NotifyCtx *ctx = data;
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(ctx->gapp));
    if (win && gtk_window_is_active(win))
        return; /* focused: color dot is enough */

    char msg[128];
    g_snprintf(msg, sizeof(msg), "%s needs input", s->name);
    dbus_notify(msg);
}

void
notify_watch(Session *s, AdwToastOverlay *overlay, GApplication *gapp)
{
    /* ponytail: ctx leaks intentionally — session lifetime == process lifetime */
    NotifyCtx *ctx           = g_new(NotifyCtx, 1);
    ctx->overlay             = overlay;
    ctx->gapp                = gapp;
    s->on_state_changed      = on_state_changed;
    s->on_state_changed_data = ctx;
}
