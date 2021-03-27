#ifndef USER_H_
#define USER_H_

#include <pthread.h>

#include "chained_list.h"
#include "config.h"

typedef struct user
{
  char username[MAX_USERNAME_LENGTH];
  int sockets_fd[MAX_SESSIONS];
  CHAINED_LIST *chained_list_followers;
  CHAINED_LIST *chained_list_notifications;
  int sessions_number;
  pthread_mutex_t mutex;
} USER;

#endif // USER_H_