#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdint.h>
#include <time.h>

typedef struct __notification
{
    uint32_t id;         // Identificador da notificação (sugere-se um identificador único)
    time_t timestamp;    // Timestamp da notificação
    uint8_t pending;     // Quantidade de leitores pendentes
    uint16_t length;     // Tamanho da mensagem
    const char *message; // Mensagem
} NOTIFICATION;

#endif // NOTIFICATION_H
