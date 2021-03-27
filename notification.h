#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdint.h>
#include <time.h>

#include "config.h"

typedef enum
{
    NOTIFICATION_TYPE__MESSAGE,
    NOTIFICATION_TYPE__INFO,
} NOTIFICATION_TYPE;

typedef struct __notification
{
    uint32_t id;                          // Identificador da notificação (sugere-se um identificador único)
    time_t timestamp;                     // Timestamp da notificação
    uint8_t pending;                      // Quantidade de leitores pendentes
    NOTIFICATION_TYPE type;               // Tipo da notificação, para saber como mostrar na tela
    char message[MAX_MESSAGE_SIZE + 2];   // Dados da mensagem
    char author[MAX_USERNAME_LENGTH + 2]; // Nome do autor da mensagem
} NOTIFICATION;

#endif // NOTIFICATION_H
