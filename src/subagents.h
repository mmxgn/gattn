#pragma once
#include <glib.h>

/* A Claude Code Agent-tool subagent, as recorded in the session transcript dir.
   These run inside the claude process, so unlike spawned children they have no
   pid -- the transcript is the only place they show up. */
typedef struct {
    char id[24];   /* agent id, from the agent-<id>.meta.json filename */
    char type[32]; /* agentType, e.g. "Explore" */
    char desc[96]; /* description, e.g. "Explore sidebar row buttons" */
} Subagent;

/* Fills `out` with the subagents of the claude session running in `cwd` that have
   not finished yet, returns how many. */
int subagents_scan(const char *cwd, Subagent *out, int max);
