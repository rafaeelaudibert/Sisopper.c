#include "server_ring.h"
#include "exit_errors.h"
#include "config.h"
#include "logger.h"
#include "notification.h"
#include "socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

extern int errno;

#define LOCK(mutex) pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)
#define max(x,y) (((x) >= (y)) ? (x) : (y))

int AVAILABLE_PORTS[] = {12550, 12551, 12552, 12553, 12554, 12555, 12556, 12557, 12558, 12559};
char *AVAILABLE_HOSTS[] = {
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
    "127.0.0.1",
};

void server_ring_create_socket(SERVER_RING *);
void server_ring_bind(SERVER_RING *);
void server_ring_listen(SERVER_RING *ring);
void server_ring_connect_with_ring(SERVER_RING *);

void start_election(void *);

SERVER_RING *server_ring_initialize(void)
{
    SERVER_RING *ring = (SERVER_RING *)malloc(sizeof(SERVER_RING));

    memset(ring->server_ring_ports, 0, sizeof(int) * MAX_RING_SIZE);
    memset(ring->server_ring_addresses, 0, sizeof(char *) * MAX_RING_SIZE);

    memcpy(ring->server_ring_ports, AVAILABLE_PORTS, max(sizeof(AVAILABLE_PORTS), sizeof(ring->server_ring_ports)));
    memcpy(ring->server_ring_addresses, AVAILABLE_HOSTS, max(sizeof(AVAILABLE_HOSTS), sizeof(ring->server_ring_addresses)));

    ring->in_election = 0; // Do NOT start in election
    ring->is_primary = 0;  // State that is not primary
    ring->self_index = -1; // We start incrementing it

    pthread_mutex_init(&ring->MUTEX_ELECTION, NULL);

    // Creating and configuring sockfd for this node to receive and send messages
    ring->self_sockfd = socket_create();
    ring->next_sockfd = socket_create();

    return ring;
}

void server_ring_connect(SERVER_RING *ring)
{
    // Configuring this server connection
    server_ring_bind(ring);
    server_ring_listen(ring);

    // Configuring this server connection with his next node in the list
    server_ring_connect_with_ring(ring);

    // Start a thread for the keep alive, to know when primary is down
    if (!ring->is_primary)
        pthread_create(&ring->keepalive_tid, NULL, (void *(*)(void *)) & server_ring_keep_alive_primary, (void *)ring);
}

void server_ring_bind(SERVER_RING *ring)
{
    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    do
    {
        // Check that we haven't finished our list of available ports
        if (ring->server_ring_ports[++ring->self_index] == 0 || ring->self_index >= MAX_RING_SIZE)
        {
            logger_error("When trying to find an available port to connect");
            exit(ERROR_BINDING_SOCKET);
        }

        serv_addr.sin_port = htons(ring->server_ring_ports[ring->self_index]);
        struct hostent *in_addr = gethostbyname(ring->server_ring_addresses[ring->self_index]);
        serv_addr.sin_addr = *((struct in_addr *)in_addr->h_addr);
    } while (bind(ring->self_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0);
}

void server_ring_listen(SERVER_RING *ring)
{
    if (listen(ring->self_sockfd, CONNECTIONS_TO_ACCEPT) < 0)
    {
        logger_error("When starting to listen");

        // Remember to close the socket already open, to free the port
        close(ring->self_sockfd);
        exit(ERROR_LISTEN);
    }

    logger_info("Listening on port %d...\n", ring->server_ring_ports[ring->self_index]);
}

void server_ring_connect_with_ring(SERVER_RING *ring)
{
    server_ring_connect_with_next_server(ring, ring->next_sockfd);

    // Went all the list around and couldn't connect to anyone, so I'm the primary
    if (ring->next_index == ring->self_index)
    {
        logger_info("Couldn't find any other option connection, so I must be the only server\n");
        logger_info("I'm the new leader! 👑\n");
        ring->is_primary = 1;
        ring->primary_idx = ring->self_index;

        return;
    }

    // Need to ask who is the primary
    logger_info("Connected with follower in port %d\n", ring->server_ring_ports[ring->next_index]);
    logger_info("Will try to find which is the primary port\n");

    NOTIFICATION notification = {.type = NOTIFICATION_TYPE__LEADER_QUESTION};
    int bytes_wrote = write(ring->next_sockfd, (void *)&notification, sizeof(NOTIFICATION));
    if (bytes_wrote < 0)
    {
        logger_error("Error when trying to sending message to find who is the current leader.\n");
        exit(ERROR_LOOKING_FOR_LEADER);
    }

    int bytes_read = read(ring->next_sockfd, (void *)&notification, sizeof(NOTIFICATION));
    if (bytes_read < 0)
    {
        logger_error("Error when trying to receive message to find who is the current leader.\n");
        exit(ERROR_LOOKING_FOR_LEADER);
    }
    if (notification.type != NOTIFICATION_TYPE__ELECTED)
    {
        logger_error("Error when trying to receive message to find who is the current leader. Received message of type %d\n", notification.type);
        exit(ERROR_LOOKING_FOR_LEADER);
    }

    ring->primary_idx = notification.data;
    logger_info("Found the primary index: %d\n", ring->primary_idx);

    close(ring->next_sockfd);
}

int server_ring_get_next_index(SERVER_RING *ring, int current_index)
{
    if (current_index - 1 >= 0)
        return current_index - 1;

    // Need to go through the port list to find what is the last index
    int last_index = MAX_RING_SIZE - 1;
    for (; ring->server_ring_ports[last_index] == 0; last_index--)
        ;

    return last_index;
}

void server_ring_keep_alive_primary(void *void_ring)
{
    pthread_t tid;
    SERVER_RING *ring = (SERVER_RING *)void_ring;

    ring->keepalive_fd = socket_create();

    // Connecting per se
    struct sockaddr_in keepalive_addr;

    keepalive_addr.sin_family = AF_INET;
    bzero(&(keepalive_addr.sin_zero), 8);

    keepalive_addr.sin_port = htons(ring->server_ring_ports[ring->primary_idx]);
    struct hostent *in_addr = gethostbyname(ring->server_ring_addresses[ring->primary_idx]);
    keepalive_addr.sin_addr = *((struct in_addr *)in_addr->h_addr);

    if (connect(ring->keepalive_fd, (struct sockaddr *)&keepalive_addr, sizeof(keepalive_addr)) < 0)
    {
        logger_error("When connecting to main server\n");
        exit(ERROR_STARTING_CONNECTION);
    }

    while (1)
    {
        logger_debug("Sending a keep alive to %d\n", ring->primary_idx);

        NOTIFICATION notification = {.type = NOTIFICATION_TYPE__KEEPALIVE}, read_notification;
        int bytes_wrote = send(ring->keepalive_fd, (void *)&notification, sizeof(NOTIFICATION), MSG_NOSIGNAL);
        if (bytes_wrote < 0)
        {
            // Master is dead, need to start an election
            if (errno == EPIPE)
            {
                logger_error("Error when sending keep alive. Master disconnected.\n");
                pthread_create(&tid, NULL, (void *(*)(void *)) & start_election, (void *)ring);
                return;
            }

            logger_error("Error when trying to send keep alive: %d\n", errno);
            exit(ERROR_LOOKING_FOR_LEADER);
        }

        int bytes_read = read(ring->keepalive_fd, (void *)&read_notification, sizeof(NOTIFICATION));
        if (bytes_read < 0)
        {
            // Connection to master timed out, need to start an election
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                logger_error("Error when receiving keep alive. Connection timed out. Master disconnected.\n");
                pthread_create(&tid, NULL, (void *(*)(void *)) & start_election, (void *)ring);
                return;
            }

            logger_error("Error when receiving keep alive.\n");
            exit(ERROR_LOOKING_FOR_LEADER);
        }

        // Sleep for some time, before checking the master again
        sleep(3);
    }
}

void start_election(void *void_ring)
{
    SERVER_RING *ring = (SERVER_RING *)void_ring;

    int sockfd = socket_create();

    logger_info("Election starting...\n");

    LOCK(ring->MUTEX_ELECTION);
    logger_info("Locked for election\n");

    if (!ring->in_election)
    {
        ring->in_election = 1;
        UNLOCK(ring->MUTEX_ELECTION);

        logger_info("Inside election start, preparing it...\n");

        server_ring_connect_with_next_server(ring, sockfd);

        // Went all the list around and couldn't connect to anyone, so I'm the primary
        if (ring->next_index == ring->self_index)
        {
            logger_info("Couldn't find any other option connection, so I must be the only server\n");
            logger_info("I'm the new leader! 👑\n");
            ring->is_primary = 1;

            // Uses mutex because we don't know if it counts as a single memory access or two because of the pointer
            LOCK(ring->MUTEX_ELECTION);
            ring->in_election = 0;
            UNLOCK(ring->MUTEX_ELECTION);

            ring->primary_idx = ring->self_index;
            return;
        }

        // Need to ask who is the primary
        logger_info("Connected with follower in port %d\n", ring->server_ring_ports[ring->next_index]);
        logger_info("Will send an election message\n");

        NOTIFICATION notification = {.type = NOTIFICATION_TYPE__ELECTION, .data = ring->self_index};
        int bytes_wrote = write(sockfd, (void *)&notification, sizeof(NOTIFICATION));
        if (bytes_wrote < 0)
        {
            logger_error("Error when trying  to send election message.\n");
            exit(ERROR_LOOKING_FOR_LEADER);
        }

        close(sockfd);
    }
    else
    {
        // If it is already in an election, can just unlock it again
        UNLOCK(ring->MUTEX_ELECTION);
    }
}

void server_ring_connect_with_next_server(SERVER_RING *ring, int sockfd)
{
    struct sockaddr_in next_addr;

    next_addr.sin_family = AF_INET;
    bzero(&(next_addr.sin_zero), 8);

    ring->next_index = ring->self_index;
    do
    {
        ring->next_index = server_ring_get_next_index(ring, ring->next_index);
        next_addr.sin_port = htons(ring->server_ring_ports[ring->next_index]);
        struct hostent *in_addr = gethostbyname(ring->server_ring_addresses[ring->next_index]);
        next_addr.sin_addr = *((struct in_addr *)in_addr->h_addr);
    } while (ring->next_index != ring->self_index && (connect(sockfd, (struct sockaddr *)&next_addr, sizeof(next_addr)) < 0));
}
