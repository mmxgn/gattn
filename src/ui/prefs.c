#include "prefs.h"
#include "../prefs_store.h"

typedef struct {
    PrefsChangedFn  on_changed;
    gpointer        data;
    GtkFontButton  *font_btn;
    GtkColorButton *fg_btn;
    GtkColorButton *bg_btn;
} PrefsCtx;

static void save_and_notify(PrefsCtx *ctx)
{
    GattnPrefs p;

    char *font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(ctx->font_btn));
    g_strlcpy(p.font, (font && *font) ? font : "Monospace 12", sizeof(p.font));
    g_free(font);

    GdkRGBA fg, bg;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(ctx->fg_btn), &fg);
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(ctx->bg_btn), &bg);
    g_snprintf(p.fg, sizeof(p.fg), "#%02x%02x%02x",
               (int)(fg.red * 255), (int)(fg.green * 255), (int)(fg.blue * 255));
    g_snprintf(p.bg, sizeof(p.bg), "#%02x%02x%02x",
               (int)(bg.red * 255), (int)(bg.green * 255), (int)(bg.blue * 255));

    prefs_save(&p);
    if (ctx->on_changed) ctx->on_changed(ctx->data);
}

static void on_font_set(GtkFontButton *b, gpointer data)
{
    (void)b;
    save_and_notify((PrefsCtx *)data);
}

static void on_color_set(GtkColorButton *b, gpointer data)
{
    (void)b;
    save_and_notify((PrefsCtx *)data);
}

void prefs_dialog_show(GtkWidget *parent, PrefsChangedFn on_changed, gpointer data)
{
    GattnPrefs p = prefs_load();

    PrefsCtx *ctx   = g_new0(PrefsCtx, 1);
    ctx->on_changed = on_changed;
    ctx->data       = data;

    /* Font */
    GtkWidget *font_btn = gtk_font_button_new_with_font(p.font);
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(font_btn), TRUE);
    ctx->font_btn = GTK_FONT_BUTTON(font_btn);
    g_signal_connect(font_btn, "font-set", G_CALLBACK(on_font_set), ctx);

    AdwActionRow *font_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_row), "Font");
    adw_action_row_add_suffix(font_row, font_btn);
    adw_action_row_set_activatable_widget(font_row, font_btn);

    /* Colors */
    GdkRGBA fg = {1,1,1,1}, bg = {0,0,0,1};
    gdk_rgba_parse(&fg, p.fg);
    gdk_rgba_parse(&bg, p.bg);

    GtkWidget *fg_btn = gtk_color_button_new_with_rgba(&fg);
    ctx->fg_btn = GTK_COLOR_BUTTON(fg_btn);
    g_signal_connect(fg_btn, "color-set", G_CALLBACK(on_color_set), ctx);

    GtkWidget *bg_btn = gtk_color_button_new_with_rgba(&bg);
    ctx->bg_btn = GTK_COLOR_BUTTON(bg_btn);
    g_signal_connect(bg_btn, "color-set", G_CALLBACK(on_color_set), ctx);

    /* Colour rows */
    AdwActionRow *fg_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(fg_row), "Text colour");
    adw_action_row_add_suffix(fg_row, fg_btn);
    adw_action_row_set_activatable_widget(fg_row, fg_btn);

    AdwActionRow *bg_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(bg_row), "Background colour");
    adw_action_row_add_suffix(bg_row, bg_btn);
    adw_action_row_set_activatable_widget(bg_row, bg_btn);

    AdwPreferencesGroup *grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp, "Terminal");
    adw_preferences_group_add(grp, GTK_WIDGET(font_row));
    adw_preferences_group_add(grp, GTK_WIDGET(fg_row));
    adw_preferences_group_add(grp, GTK_WIDGET(bg_row));

    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "Appearance");
    adw_preferences_page_add(page, grp);

    AdwPreferencesWindow *win = ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
    adw_preferences_window_add(win, page);
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 300);

    GtkRoot *root = gtk_widget_get_root(parent);
    if (root) gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(root));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);

    g_object_set_data_full(G_OBJECT(win), "prefs-ctx", ctx, g_free);

    gtk_window_present(GTK_WINDOW(win));
}
