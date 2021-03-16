#include "hash.h"
#include <ctype.h>

HASH_TABLE hash_init(void)
{
    HASH_TABLE table;

    table = (HASH_TABLE)calloc(HASH_SIZE, sizeof(HASH_NODE *));
    return table;
}

void hash_free(HASH_TABLE table)
{
    HASH_NODE *node, *next_node;

    if (!table)
        return;

    // For each position in the table
    for (int table_idx = 0; table_idx < HASH_SIZE; table_idx++)
    {
        // Get the pointer in the current position
        node = table[table_idx];

        // While pointing at something that is not NULL
        while (node)
        {
            // Save the next
            next_node = node->next;

            // Free the text, and the node itself
            free(node->text);
            free(node);

            // Now we will look for the next node
            node = next_node;
        }
    }

    // Finally, free the table pointer
    free(table);
}

int hash_address(char *text)
{
    int address = 1;
    int text_idx;

    // Initializing in one, and taking modulo (HASH_SIZE + 1)
    // we are in range [1, HASH_SIZE + 1)
    for (text_idx = 0; text_idx < strlen(text); text_idx++)
        address = (address * text[text_idx]) % HASH_SIZE + 1;

    // We decrement 1 to be in the range [0, HASH_SIZE),
    //so that we can access the table correctly
    return address - 1;
}

HASH_NODE *hash_find(HASH_TABLE table, char *text)
{
    if (!table)
        return NULL;

    int address = hash_address(text);
    HASH_NODE *node = table[address];

    while (node)
    {
        if (strcmp(text, node->text) == 0)
            return node;

        node = node->next;
    }

    return NULL;
}

HASH_NODE *hash_insert(HASH_TABLE table, char *text, void *content, int type)
{
    if (!table)
        return NULL;

    HASH_NODE *new_node = hash_find(table, text);

    if (!new_node)
    {
        new_node = (HASH_NODE *)calloc(1, sizeof(HASH_NODE));
        new_node->content = content;
        new_node->type = type;
        new_node->text = (char *)calloc(strlen(text) + 1, sizeof(char));
        strcpy(new_node->text, text);

        int address = hash_address(text);
        new_node->next = table[address];
        table[address] = new_node;
    }

    return new_node;
}

void hash_print(HASH_TABLE table)
{
    if (!table)
        return;

    int list_idx;
    HASH_NODE *node;

    printf("Hash: \n");
    for (int table_idx = 0; table_idx < HASH_SIZE; table_idx++)
        for (node = table[table_idx], list_idx = 0; node; node = node->next, list_idx++)
            printf(
                "Table[%d][%d] -> %s (Type %d)\n",
                table_idx,
                list_idx,
                node->text,
                node->type);
}
