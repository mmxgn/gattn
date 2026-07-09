#pragma once
#include "../session.h"

GtkWidget *sidebar_new(SessionList *sessions);
void       sidebar_add_session(GtkWidget *split, Session *s);
