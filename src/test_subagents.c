/* Self-check for subagents_scan: builds a fake ~/.claude transcript layout and asserts
   that a subagent whose tool_use_id has no tool_result yet is reported as running,
   while a finished one is not. */
#include "subagents.h"
#include <glib/gstdio.h>
#include <unistd.h>

int
main(void)
{
    /* Set HOME before touching any glib path helper -- glib caches the home dir on
       the first such call. */
    char *home = g_strdup_printf("/tmp/gattn-subagents-test-%d", (int)getpid());
    g_setenv("HOME", home, TRUE);

    char *proj = g_build_filename(home, ".claude", "projects", "-tmp-fake-project", NULL);
    char *subs = g_build_filename(proj, "sess-1", "subagents", NULL);
    g_assert_cmpint(g_mkdir_with_parents(subs, 0700), ==, 0);

    char *tr = g_build_filename(proj, "sess-1.jsonl", NULL);
    g_assert_true(g_file_set_contents(tr,
                                      "{\"type\":\"assistant\"}\n"
                                      "{\"type\":\"user\",\"content\":"
                                      "[{\"tool_use_id\":\"toolu_done\"}]}\n",
                                      -1, NULL));

    char *live = g_build_filename(subs, "agent-aaa111.meta.json", NULL);
    g_assert_true(g_file_set_contents(live,
                                      "{\"agentType\":\"Explore\","
                                      "\"description\":\"Explore sidebar rows\","
                                      "\"toolUseId\":\"toolu_live\",\"spawnDepth\":1}",
                                      -1, NULL));
    char *done = g_build_filename(subs, "agent-bbb222.meta.json", NULL);
    g_assert_true(g_file_set_contents(done,
                                      "{\"agentType\":\"Plan\","
                                      "\"description\":\"Plan the thing\","
                                      "\"toolUseId\":\"toolu_done\",\"spawnDepth\":1}",
                                      -1, NULL));

    Subagent found[8];
    g_assert_cmpint(subagents_scan("/tmp/fake-project", found, 8), ==, 1);
    g_assert_cmpstr(found[0].id, ==, "aaa111");
    g_assert_cmpstr(found[0].type, ==, "Explore");
    g_assert_cmpstr(found[0].desc, ==, "Explore sidebar rows");

    /* A directory with no claude transcripts at all: empty, no crash. */
    g_assert_cmpint(subagents_scan("/tmp/no-such-project-xyz", found, 8), ==, 0);
    g_assert_cmpint(subagents_scan("", found, 8), ==, 0);

    g_remove(live);
    g_remove(done);
    g_remove(tr);
    g_rmdir(subs);
    char *sess = g_build_filename(proj, "sess-1", NULL);
    g_rmdir(sess);
    g_rmdir(proj);
    g_free(sess);
    g_free(live);
    g_free(done);
    g_free(tr);
    g_free(subs);
    g_free(proj);
    g_free(home);
    return 0;
}
