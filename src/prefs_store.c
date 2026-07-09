#include "prefs_store.h"
#include <glib.h>
#include <string.h>

#define GROUP "terminal"

static char *
prefs_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "gattn", "prefs.ini", NULL);
}

GattnPrefs
prefs_load(void)
{
    GattnPrefs p    = { "Monospace 12", "#ffffff", "#000000" };
    char      *path = prefs_path();
    GKeyFile  *kf   = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        char *v;
        if ((v = g_key_file_get_string(kf, GROUP, "font", NULL))) {
            g_strlcpy(p.font, v, sizeof(p.font));
            g_free(v);
        }
        if ((v = g_key_file_get_string(kf, GROUP, "fg", NULL))) {
            g_strlcpy(p.fg, v, sizeof(p.fg));
            g_free(v);
        }
        if ((v = g_key_file_get_string(kf, GROUP, "bg", NULL))) {
            g_strlcpy(p.bg, v, sizeof(p.bg));
            g_free(v);
        }
    }
    g_key_file_unref(kf);
    g_free(path);
    return p;
}

void
prefs_save(const GattnPrefs *p)
{
    char *path = prefs_path();
    char *dir  = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, GROUP, "font", p->font);
    g_key_file_set_string(kf, GROUP, "fg", p->fg);
    g_key_file_set_string(kf, GROUP, "bg", p->bg);
    g_key_file_save_to_file(kf, path, NULL);
    g_key_file_unref(kf);
    g_free(path);
}
