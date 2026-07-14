#pragma once
#include "session.h"

typedef struct {
    char cmd[128];
    char dir[512];
    char name[64];
} SavedSession;

int  sessions_load(SavedSession *out, int max);
void sessions_save(SessionList *list);
