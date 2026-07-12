#pragma once

typedef struct {
    char font[128];
    char fg[16];
    char bg[16];
    char theme[32];        /* "Custom" or a name from prefs.c's THEMES table */
    char color_scheme[16]; /* "auto", "light", or "dark" */
} GattnPrefs;

GattnPrefs prefs_load(void);
void       prefs_save(const GattnPrefs *p);
