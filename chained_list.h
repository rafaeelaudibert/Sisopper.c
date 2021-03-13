#ifndef CHAINED_LIST_H
#define CHAINED_LIST_H

typedef struct chained_list
{
    void *val;
    struct chained_list *next;
} CHAINED_LIST;

CHAINED_LIST *chained_list_create(void *val);
CHAINED_LIST *chained_list_reverse(CHAINED_LIST *list);
CHAINED_LIST *chained_list_append_end(CHAINED_LIST *list, void *val);
void chained_list_free(CHAINED_LIST *list);
void chained_list_print(CHAINED_LIST *list, void (*item_print_function)(void *));
void chained_list_iterate(CHAINED_LIST *list, void (*iterate_function)(void *));

#endif