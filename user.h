#define MAX_SESSIONS 1
#define MAX_USERNAME_LENGHT 20

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chained_list.h"

#define HASH_SIZE 1999

typedef struct user
{
  int sockets_fd[MAX_SESSIONS];
  CHAINED_LIST *chained_list_followers;
  CHAINED_LIST *chained_list_notifications;
  int sessions_number;
} USER;

typedef struct hash_user
{
  char username[MAX_USERNAME_LENGHT];
  USER user;
  struct hash_user *next;
} HASH_USER;

void hashInit(void);
int hashAddress(char *username);
HASH_USER *hashFind(char *username);
HASH_USER *hashInsert(char *username, USER user);
void hashPrint(void);
