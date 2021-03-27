#include <pthread.h>
#include <stdlib.h>

#include "user.h"

USER *init_user(void)
{
    USER *user = (USER *)calloc(1, sizeof(USER));

    pthread_mutex_init(&user->mutex, NULL);
    user->followers = NULL;
    user->notifications = NULL;
    user->pending_notifications = NULL;
    user->sessions_number = 0;

    for (int i = 0; i < MAX_SESSIONS; i++)
        user->sockets_fd[i] = -1;

    return user;
}