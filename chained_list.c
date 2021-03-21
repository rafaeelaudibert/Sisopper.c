#include <stdlib.h>
#include <stdio.h>
#include "chained_list.h"

/// Creates a CHAINED_LIST node, pointing to NULL, and a val as `val`
///
/// @param val A pointer to void value, which this CHAINED_LIST will point to
///
/// @returns A new pointer to a CHAINED_LIST node
CHAINED_LIST *chained_list_create(void *val)
{
    CHAINED_LIST *cl = (CHAINED_LIST *)malloc(sizeof(CHAINED_LIST));
    cl->next = NULL;
    cl->val = (void *)val;

    return cl;
}

/// Reverts a chainedList, this is:
///     if we have a list a -> b -> c -> NULL, we will have a list c -> b -> a -> NULL
/// Important to notice that this happens inplace
///
/// @param list A CHAINED_LIST which will be reverted. This action will destroy the list
///
/// @returns A new pointer to a CHAINED_LIST node
CHAINED_LIST *chained_list_revert(CHAINED_LIST *list)
{
    CHAINED_LIST *next = NULL, *before = NULL, *curr = list;
    while (curr)
    {
        next = curr->next;
        curr->next = before;

        before = curr;
        curr = next;
    }

    return before;
}

/// Appends a value to the end of a CHAINED_LIST
///
/// @param list CHAINED_LIST* list which will have a element inserted
/// @param val void* value which will be appended to the [list]
///
/// @returns The appended CHAINED_LIST* start node
CHAINED_LIST *chained_list_append_end(CHAINED_LIST *list, void *val)
{
    CHAINED_LIST *new_list = chained_list_create(val);

    if (!list)
        return new_list;

    CHAINED_LIST *end_list = list;
    while (end_list->next)
        end_list = end_list->next;

    end_list->next = new_list;

    return list;
}

/// Frees entirely a CHAINED_LIST*. It DOES NOT frees the val inside of it
///
/// @param list A CHAINED_LIST* to be freed
void chained_list_free(CHAINED_LIST *list)
{
    CHAINED_LIST *next = NULL;
    while (list)
    {
        next = list->next;
        free(list);

        list = next;
    }
}

/// Prints a chained list
///
/// @param list A CHAINED_LIST* to be printed
/// @param item_print_function A function which receives each item val, to handle how to print the val
void chained_list_print(CHAINED_LIST *list, void (*item_print_function)(void *))
{
    printf("[");
    while (list)
    {
        item_print_function(list->val);
        if (list->next)
            printf(", ");

        list = list->next;
    }
    printf("]\n");
}

/// Iterates over a list
///
/// @param list A CHAINED_LIST* to be printed
/// @param iterate_function A function which is called for every value in the list
void chained_list_iterate(CHAINED_LIST *list, void (*iterate_function)(void *))
{
    while (list)
    {
        iterate_function(list->val);
        list = list->next;
    }
}
