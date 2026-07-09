#include "session.h"
#include <string.h>

void session_list_init(SessionList *list)
{
    list->count = 0;
}

Session *session_create(SessionList *list, const char *name)
{
    if (list->count >= 32)
        return NULL;
    Session *s = &list->items[list->count++];
    s->id       = list->count;
    s->state    = SESSION_IDLE;
    s->terminal = NULL;
    s->pid      = 0;
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    return s;
}

void session_destroy(SessionList *list, int id)
{
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].id != id)
            continue;
        for (int j = i; j < list->count - 1; j++)
            list->items[j] = list->items[j + 1];
        list->count--;
        return;
    }
}

void session_set_state(Session *s, SessionState state)
{
    s->state = state;
}

#ifdef SESSION_TEST
#include <assert.h>
#include <stdio.h>
int main(void)
{
    SessionList list;
    session_list_init(&list);
    assert(list.count == 0);

    Session *s = session_create(&list, "claude");
    assert(s && list.count == 1 && s->state == SESSION_IDLE);

    session_set_state(s, SESSION_WORKING);
    assert(s->state == SESSION_WORKING);

    int id = s->id;
    session_destroy(&list, id);
    assert(list.count == 0);

    puts("session: ok");
    return 0;
}
#endif
