#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdint.h>
#include <time.h>

#include "config.h"

typedef enum Command
{
    SEND = 1,
    FOLLOW,
    UNKNOWN,
} COMMAND;

typedef enum
{
    NOTIFICATION_TYPE__MESSAGE,
    NOTIFICATION_TYPE__INFO,
    NOTIFICATION_TYPE__LEADER_QUESTION,
    NOTIFICATION_TYPE__ELECTION,
    NOTIFICATION_TYPE__ELECTED,
    NOTIFICATION_TYPE__LOGIN,
    NOTIFICATION_TYPE__KEEPALIVE,
    NOTIFICATION_TYPE__REPLICATION,
    NOTIFICATION_TYPE__WIPE,
} NOTIFICATION_TYPE;

typedef struct __notification
{
    COMMAND command;
    uint32_t id;                            // Identificador da notificação (sugere-se um identificador único)
    time_t timestamp;                       // Timestamp da notificação
    NOTIFICATION_TYPE type;                 // Tipo da notificação, para saber como mostrar na tela
    char message[MAX_MESSAGE_SIZE + 2];     // Dados da mensagem
    char author[MAX_USERNAME_LENGTH + 2];   // Nome do autor da mensagem
    int data;                               // Dados inteiros passados quando estamos usando LEADER_QUESTION, ELECTION ou ELECTED
    char receiver[MAX_USERNAME_LENGTH + 2]; // Nome do usuario que vai receber a notificação
} NOTIFICATION;

#endif // NOTIFICATION_H
