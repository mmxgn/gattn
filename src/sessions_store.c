#include "sessions_store.h"
#include <glib.h>

static char *
store_path(void)
{
    return g_build_filename(g_get_user_data_dir(), "gattn", "sessions.txt", NULL);
}

int
sessions_load(SavedSession *out, int max)
{
    char *path     = store_path();
    char *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        g_free(path);
        return 0;
    }
    g_free(path);
    g_strstrip(contents);

    char **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    int n = 0;
    for (int i = 0; lines[i] && n < max; i++) {
        if (!lines[i][0])
            continue;
        /* Format: cmd|dir[|name]. Older files omit the name field. */
        char **parts = g_strsplit(lines[i], "|", 3);
        if (parts[0] && parts[1]) {
            g_strlcpy(out[n].cmd, parts[0], sizeof(out[n].cmd));
            g_strlcpy(out[n].dir, parts[1], sizeof(out[n].dir));
            out[n].name[0] = '\0';
            if (parts[2])
                g_strlcpy(out[n].name, parts[2], sizeof(out[n].name));
            n++;
        }
        g_strfreev(parts);
    }
    g_strfreev(lines);
    return n;
}

void
sessions_save(SessionList *list)
{
    char *path = store_path();
    char *dir  = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    GString *buf = g_string_new(NULL);
    for (int i = 0; i < list->count; i++) {
        Session *s = list->items[i];
        if (!s || s->parent_id != 0 || !s->cwd[0])
            continue;
        g_string_append_printf(buf, "%s|%s|%s\n", s->cmd, s->cwd, s->name);
    }
    g_file_set_contents(path, buf->str, -1, NULL);
    g_string_free(buf, TRUE);
    g_free(path);
}
