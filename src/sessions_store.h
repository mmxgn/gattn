#pragma once
#include "session.h"

typedef struct {
    char cmd[128];
    char dir[512];
} SavedSession;

int  sessions_load(SavedSession *out, int max);
void sessions_save(SessionList *list);
