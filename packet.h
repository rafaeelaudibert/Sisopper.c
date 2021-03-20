#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <time.h>

#include "config.h"

typedef enum Command {
		SEND = 1,
		FOLLOW,
    UNKNOWN,
	} COMMAND;

typedef struct __packet
{
    COMMAND command;
    uint16_t seqn;                      // Número de sequência
    uint16_t length;                    // Comprimento do payload
    time_t timestamp;                   // Timestamp do dado
    char payload[MAX_MESSAGE_SIZE + 2]; // Dados da mensagem
} PACKET;

#endif // PACKET_H
