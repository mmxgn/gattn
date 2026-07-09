#pragma once

typedef struct {
    char font[128];
    char fg[16];
    char bg[16];
} GattnPrefs;

GattnPrefs prefs_load(void);
void       prefs_save(const GattnPrefs *p);
