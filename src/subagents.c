#include "subagents.h"
#include <glib/gstdio.h>
#include <string.h>

/* Pull a flat "key":"value" out of a small JSON object.
   ponytail: no unescaping -- agentType and description are short plain text written
   by claude itself. One containing a quote gets clipped; add an unescaper if that
   ever shows up in practice. */
static void
json_str(const char *json, const char *key, char *out, gsize out_sz)
{
    out[0] = '\0';
    char needle[32];
    g_snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(json, needle);
    if (!p)
        return;
    p += strlen(needle);
    const char *e = strchr(p, '"');
    if (!e)
        return;
    g_strlcpy(out, p, MIN((gsize)(e - p) + 1, out_sz));
}

/* Newest *.jsonl in `dir`, extension stripped. Caller frees; NULL if none. */
static char *
newest_session_id(const char *dir)
{
    GDir *d = g_dir_open(dir, 0, NULL);
    if (!d)
        return NULL;

    const char *e;
    char       *best    = NULL;
    time_t      best_mt = 0;
    while ((e = g_dir_read_name(d))) {
        if (e[0] == '.' || !g_str_has_suffix(e, ".jsonl"))
            continue;
        char    *full = g_build_filename(dir, e, NULL);
        GStatBuf st;
        if (g_stat(full, &st) == 0 && st.st_mtime >= best_mt) {
            best_mt = st.st_mtime;
            g_free(best);
            best = g_strndup(e, strlen(e) - strlen(".jsonl"));
        }
        g_free(full);
    }
    g_dir_close(d);
    return best;
}

int
subagents_scan(const char *cwd, Subagent *out, int max)
{
    if (!cwd || !cwd[0] || max <= 0)
        return 0;

    /* ~/.claude/projects keys a project by its abs path with every '/' -> '-' */
    char *key = g_strdup(cwd);
    for (char *c = key; *c; c++)
        if (*c == '/')
            *c = '-';
    char *proj = g_build_filename(g_get_home_dir(), ".claude", "projects", key, NULL);
    g_free(key);

    /* ponytail: freshest transcript in the project dir is this session's -- the same
       heuristic the fork button uses. Two claude sessions in one cwd will show each
       other's subagents; a real fix needs a pid -> session-id mapping that claude
       does not currently expose. */
    char *sid = newest_session_id(proj);
    if (!sid) {
        g_free(proj);
        return 0;
    }

    /* A finished Agent call leaves its tool_result in the parent transcript, so the
       presence of the tool_use_id there is what marks a subagent done.
       ponytail: re-reads the whole transcript each poll. It is a few hundred KB and
       one strstr per subagent; switch to a tail scan if that ever shows up in a
       profile. */
    char *tpath      = g_strdup_printf("%s/%s.jsonl", proj, sid);
    char *transcript = NULL;
    g_file_get_contents(tpath, &transcript, NULL, NULL);
    g_free(tpath);

    char *sub_dir = g_build_filename(proj, sid, "subagents", NULL);
    g_free(proj);
    g_free(sid);

    GDir       *d = g_dir_open(sub_dir, 0, NULL);
    const char *e;
    int         n = 0;
    while (d && n < max && (e = g_dir_read_name(d))) {
        if (!g_str_has_prefix(e, "agent-") || !g_str_has_suffix(e, ".meta.json"))
            continue;

        char    *path = g_build_filename(sub_dir, e, NULL);
        char    *meta = NULL;
        gboolean ok   = g_file_get_contents(path, &meta, NULL, NULL);
        g_free(path);
        if (!ok)
            continue;

        char tool_use_id[64];
        json_str(meta, "toolUseId", tool_use_id, sizeof(tool_use_id));
        if (transcript && tool_use_id[0]) {
            char needle[96];
            g_snprintf(needle, sizeof(needle), "\"tool_use_id\":\"%s\"", tool_use_id);
            if (strstr(transcript, needle)) {
                g_free(meta);
                continue;
            }
        }

        json_str(meta, "agentType", out[n].type, sizeof(out[n].type));
        json_str(meta, "description", out[n].desc, sizeof(out[n].desc));
        g_free(meta);

        gsize idlen = strlen(e) - strlen("agent-") - strlen(".meta.json");
        g_strlcpy(out[n].id, e + strlen("agent-"), MIN(idlen + 1, sizeof(out[n].id)));
        n++;
    }
    if (d)
        g_dir_close(d);
    g_free(sub_dir);
    g_free(transcript);
    return n;
}
