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
    int primary_idx;
    int primary_fd;

    int keepalive_fd;
    pthread_t keepalive_tid;

    int in_election;
    pthread_mutex_t MUTEX_ELECTION;
} SERVER_RING;

SERVER_RING *server_ring_initialize(void);
void server_ring_connect(SERVER_RING *);
int server_ring_get_next_index(SERVER_RING *, int);
void server_ring_keep_alive_primary(void *);
void server_ring_connect_with_next_server(SERVER_RING *, int);

#endif // SERVER_RING_H