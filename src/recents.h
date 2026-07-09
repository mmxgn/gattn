#pragma once
void   recents_add(const char *path);
char **recents_list(void); /* free with g_strfreev */
