#include "notify.h"

typedef struct {
    AdwToastOverlay *overlay;
    GApplication    *gapp;
} NotifyCtx;

static void on_state_changed(Session *s, gpointer data)
{
    if (s->state != SESSION_NEEDS_INPUT) return;

    NotifyCtx *ctx = data;
    char msg[128];
    g_snprintf(msg, sizeof(msg), "%s needs input", s->name);

    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(ctx->gapp));

    if (win && gtk_window_is_active(win)) {
        AdwToast *toast = adw_toast_new(msg);
        adw_toast_set_timeout(toast, 3);
        adw_toast_overlay_add_toast(ctx->overlay, toast);
    } else {
        GNotification *notif = g_notification_new("gattn");
        g_notification_set_body(notif, msg);
        g_notification_set_default_action_and_target(
            notif, "app.raise", NULL);
        g_application_send_notification(ctx->gapp, s->name, notif);
        g_object_unref(notif);
    }
}

void notify_watch(Session *s, AdwToastOverlay *overlay, GApplication *gapp)
{
    /* ponytail: ctx leaks intentionally — session lifetime == process lifetime */
    NotifyCtx *ctx = g_new(NotifyCtx, 1);
    ctx->overlay          = overlay;
    ctx->gapp             = gapp;
    s->on_state_changed      = on_state_changed;
    s->on_state_changed_data = ctx;
}
