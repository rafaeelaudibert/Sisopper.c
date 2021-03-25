#ifndef USER_H_
#define USER_H_

#include "chained_list.h"
#include "config.h"

typedef struct user
{
  char username[MAX_USERNAME_LENGTH];
  int sockets_fd[MAX_SESSIONS];
  CHAINED_LIST *chained_list_followers;
  CHAINED_LIST *chained_list_notifications;
  int sessions_number;
} USER;

#endif // USER_H_