#ifndef SERVER_RING_H
#define SERVER_RING_H

#define MAX_RING_SIZE 20

#include <pthread.h>

typedef struct server_ring
{
    int server_ring_ports[MAX_RING_SIZE];
    char *server_ring_addresses[MAX_RING_SIZE];

    int next_index;
    int next_sockfd;

    int self_index;
    int self_sockfd;

    int is_primary;
    int primary_port;

    int keepalive_fd;
    pthread_t keepalive_tid;
} SERVER_RING;

SERVER_RING *server_ring_initialize(void);
void server_ring_connect(SERVER_RING *);

#endif // SERVER_RING_H