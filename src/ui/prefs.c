#include "prefs.h"
#include "../prefs_store.h"
#include <string.h>

/* Preset themes. First entry is the default. "Custom" is appended in the combo. */
static const struct {
    const char *name, *fg, *bg;
} THEMES[] = {
    { "Default", "#ffffff", "#000000" },
    { "Solarized Dark", "#839496", "#002b36" },
    { "Solarized Light", "#657b83", "#fdf6e3" },
    { "Dracula", "#f8f8f2", "#282a36" },
    { "Nord", "#d8dee9", "#2e3440" },
    { "Gruvbox Dark", "#ebdbb2", "#282828" },
    { "Monokai", "#f8f8f2", "#272822" },
    { "Tokyo Night", "#c0caf5", "#1a1b26" },
    { "One Dark", "#abb2bf", "#282c34" },
};
#define N_THEMES (sizeof(THEMES) / sizeof(*THEMES))
#define CUSTOM_ID N_THEMES

typedef struct {
    PrefsChangedFn  on_changed;
    gpointer        data;
    GtkFontButton  *font_btn;
    GtkColorButton *fg_btn;
    GtkColorButton *bg_btn;
    AdwComboRow    *theme_row;
    gboolean        syncing; /* guard against combo→colors→combo feedback */
} PrefsCtx;

static void
apply_hex(GtkColorButton *btn, const char *hex)
{
    GdkRGBA c = { 0, 0, 0, 1 };
    gdk_rgba_parse(&c, hex);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(btn), &c);
}

static void
save_and_notify(PrefsCtx *ctx)
{
    GattnPrefs p;

    char *font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(ctx->font_btn));
    g_strlcpy(p.font, (font && *font) ? font : "Monospace 12", sizeof(p.font));
    g_free(font);

    GdkRGBA fg, bg;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(ctx->fg_btn), &fg);
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(ctx->bg_btn), &bg);
    g_snprintf(p.fg, sizeof(p.fg), "#%02x%02x%02x", (int)(fg.red * 255), (int)(fg.green * 255),
               (int)(fg.blue * 255));
    g_snprintf(p.bg, sizeof(p.bg), "#%02x%02x%02x", (int)(bg.red * 255), (int)(bg.green * 255),
               (int)(bg.blue * 255));

    guint sel = adw_combo_row_get_selected(ctx->theme_row);
    g_strlcpy(p.theme, sel < N_THEMES ? THEMES[sel].name : "Custom", sizeof(p.theme));

    prefs_save(&p);
    if (ctx->on_changed)
        ctx->on_changed(ctx->data);
}

static void
on_font_set(GtkFontButton *b, gpointer data)
{
    (void)b;
    save_and_notify((PrefsCtx *)data);
}

static void
on_color_set(GtkColorButton *b, gpointer data)
{
    (void)b;
    PrefsCtx *ctx = data;
    if (!ctx->syncing) {
        /* Manual colour edit → drop preset, mark Custom. */
        adw_combo_row_set_selected(ctx->theme_row, CUSTOM_ID);
    }
    save_and_notify(ctx);
}

static void
on_theme_selected(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    PrefsCtx *ctx = data;
    if (ctx->syncing)
        return;
    guint sel = adw_combo_row_get_selected(row);
    if (sel >= N_THEMES)
        return; /* Custom — leave colours as-is */

    ctx->syncing = TRUE;
    apply_hex(ctx->fg_btn, THEMES[sel].fg);
    apply_hex(ctx->bg_btn, THEMES[sel].bg);
    ctx->syncing = FALSE;
    save_and_notify(ctx);
}

void
prefs_dialog_show(GtkWidget *parent, PrefsChangedFn on_changed, gpointer data)
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

    /* Theme */
    const char *names[N_THEMES + 2] = { 0 };
    for (size_t i = 0; i < N_THEMES; i++)
        names[i] = THEMES[i].name;
    names[N_THEMES]      = "Custom";
    GtkStringList *model = gtk_string_list_new(names);

    AdwComboRow *theme_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(theme_row), "Theme");
    adw_combo_row_set_model(theme_row, G_LIST_MODEL(model));
    guint initial = CUSTOM_ID;
    for (size_t i = 0; i < N_THEMES; i++)
        if (strcmp(THEMES[i].name, p.theme) == 0) {
            initial = i;
            break;
        }
    adw_combo_row_set_selected(theme_row, initial);
    ctx->theme_row = theme_row;

    /* Colors */
    GdkRGBA fg = { 1, 1, 1, 1 }, bg = { 0, 0, 0, 1 };
    gdk_rgba_parse(&fg, p.fg);
    gdk_rgba_parse(&bg, p.bg);

    GtkWidget *fg_btn = gtk_color_button_new_with_rgba(&fg);
    ctx->fg_btn       = GTK_COLOR_BUTTON(fg_btn);
    g_signal_connect(fg_btn, "color-set", G_CALLBACK(on_color_set), ctx);

    GtkWidget *bg_btn = gtk_color_button_new_with_rgba(&bg);
    ctx->bg_btn       = GTK_COLOR_BUTTON(bg_btn);
    g_signal_connect(bg_btn, "color-set", G_CALLBACK(on_color_set), ctx);

    /* Wire the theme signal *after* colour buttons exist. */
    g_signal_connect(theme_row, "notify::selected", G_CALLBACK(on_theme_selected), ctx);

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
    adw_preferences_group_add(grp, GTK_WIDGET(theme_row));
    adw_preferences_group_add(grp, GTK_WIDGET(fg_row));
    adw_preferences_group_add(grp, GTK_WIDGET(bg_row));

    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "Appearance");
    adw_preferences_page_add(page, grp);

    AdwPreferencesWindow *win = ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
    adw_preferences_window_add(win, page);
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 360);

    GtkRoot *root = gtk_widget_get_root(parent);
    if (root)
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(root));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);

    g_object_set_data_full(G_OBJECT(win), "prefs-ctx", ctx, g_free);

    gtk_window_present(GTK_WINDOW(win));
}
