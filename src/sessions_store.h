#pragma once
#include "session.h"

typedef struct {
    char cmd[128];
    char dir[512];
    char name[64];
    char fork_parent_dir[512]; /* non-empty if this session is a fork; value = parent's cwd */
} SavedSession;

int  sessions_load(SavedSession *out, int max);
void sessions_save(SessionList *list);
