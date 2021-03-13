#ifndef CHAINED_LIST_H
#define CHAINED_LIST_H

typedef struct CHAINED_LIST
{
    void *val;
    struct ChainedList *next;
} CHAINED_LIST;

CHAINED_LIST *chained_list_create(void *val);
CHAINED_LIST *chained_list_reverse(CHAINED_LIST *list);
CHAINED_LIST *chained_list_append_end(CHAINED_LIST *list, void *val);
void chained_list_free(CHAINED_LIST *list);
void chained_list_print(CHAINED_LIST *list, void (*item_print_function)(void *));

#endif