#ifndef HASH_H
#define HASH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HASH_SIZE 1999

typedef struct hash_node
{
    int type;
    char *text;
    void *content;
    struct hash_node *next;
} HASH_NODE;

// C syntax is confuse here but HASH_TABLE is a HASH_NODE**
typedef HASH_NODE **HASH_TABLE;

HASH_TABLE hash_init(void);
void hash_free(HASH_TABLE table);
int hash_address(char *text);
HASH_NODE *hash_find(HASH_TABLE table, char *text);
HASH_NODE *hash_insert(HASH_TABLE table, char *text, void *content, int type);
void hash_print(HASH_TABLE table);

#endif
