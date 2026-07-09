#include "recents.h"
#include <glib.h>
#include <string.h>

static char *recents_file(void)
{
    return g_build_filename(g_get_user_data_dir(), "gattn", "recent-dirs.txt", NULL);
}

char **recents_list(void)
{
    char *path = recents_file();
    char *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        g_free(path);
        return NULL;
    }
    g_free(path);

    g_strstrip(contents);
    char    **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    GPtrArray *out = g_ptr_array_new();
    for (int i = 0; lines[i]; i++)
        if (lines[i][0])
            g_ptr_array_add(out, g_strdup(lines[i]));
    g_strfreev(lines);
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

void recents_add(const char *path)
{
    char **old  = recents_list();
    char  *file = recents_file();
    char  *dir  = g_path_get_dirname(file);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    GString *buf = g_string_new(path);
    g_string_append_c(buf, '\n');
    int count = 1;
    for (int i = 0; old && old[i] && count < 10; i++) {
        if (strcmp(old[i], path) != 0) {
            g_string_append(buf, old[i]);
            g_string_append_c(buf, '\n');
            count++;
        }
    }
    g_strfreev(old);
    g_file_set_contents(file, buf->str, -1, NULL);
    g_string_free(buf, TRUE);
    g_free(file);
}
