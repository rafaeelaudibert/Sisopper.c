#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>

#include "chained_list.h"
#include "exit_errors.h"
#include "logger.h"
#include "user.h"
#include "hash.h"
#include "notification.h"
#include "savefile.h"
#include "server_ring.h"
#include "socket.h"

#include "config.h"

#define LOCK(mutex) pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)

#define TRUE 1
#define FALSE 0

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;

static int received_sigint = FALSE;

int serverRM_socket;
int serverRM_keepalive_socket;
int client_connections_socket;
int serverRM_online = FALSE;

pthread_t reconnect_tid;
pthread_t message_consumer_tid;

unsigned long long GLOBAL_NOTIFICATION_ID = 0;

HASH_TABLE user_hash_table = NULL;

NOTIFICATION *processing_message;

// Mutexes
pthread_mutex_t MUTEX_RECONNECT = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_ONLINE_SERVER = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_CONSUMER = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_MESSAGE_QUEUE = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_APPEND_LIST = PTHREAD_MUTEX_INITIALIZER;

void sigint_handler(int);
void handle_signals(void);
void *monitor_connection_keep_alive(void *);
int handle_server_connection(struct sockaddr_in *);
void *listen_server_connection(void *);
void *keep_server_connection(void);
void *listen_message_processor(void);
void *listen_client_connection(void *);
void process_client_message(NOTIFICATION *);
void send_server(NOTIFICATION *);
int send_notification(NOTIFICATION *, int);
void cleanup(int);

SERVER_RING *ring;
int IS_CONNECTED_TO_SERVER = FALSE;

int main(int argc, char *argv[])
{
    // Sockets Address Config
    struct sockaddr_in serv_addr, client_addr;

    handle_signals();

    // Server reconnect is responsible to keep the connection to the RM
    pthread_create(&reconnect_tid, NULL, (void *(*)(void *)) & keep_server_connection, NULL);

    // Creating this socket
    int sockfd = socket_create();

    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    for (int i = 0; i < (NUMBER_OF_FES) + 1; i++)
    {
        if (i == NUMBER_OF_FES)
        {
            // We have no more FEs to connect to
            logger_error("When trying to bind to the available FE ports\n");
            exit(ERROR_BINDING_SOCKET);
        }

        port = FE_PORTS[i];
        serv_addr.sin_port = htons(port);
        struct hostent *in_addr = gethostbyname(FE_HOSTS[i]);
        serv_addr.sin_addr = *((struct in_addr *)in_addr->h_addr);

        // If can bind, break out of here
        if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0)
            break;
    }

    if (listen(sockfd, CONNECTIONS_TO_ACCEPT) < 0)
    {
        logger_error("When starting to listen");

        // Remember to close the socket already open, to free the port
        close(sockfd);
        exit(ERROR_LISTEN);
    }

    logger_info("Listening on port %d...\n", PORT);

    // Incoming message listener
    pthread_create(&message_consumer_tid, NULL, (void *(*)(void *)) & listen_message_processor, NULL);

    // Variables used below to connect to the incoming client connections
    socklen_t clilen = sizeof(struct sockaddr_in);
    struct sockaddr_in cli_addr;

    while (TRUE)
    {
        int *newsockfd = (int *)malloc(sizeof(int));

        if ((*newsockfd = accept(client_connections_socket, (struct sockaddr *)&cli_addr, &clilen)) < 0)
        {
            logger_error("When accepting client connection\n");
            cleanup(ERROR_ACCEPT);
        }

        logger_info("New Client connection from %s:%d\n", inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);

        pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
        pthread_create(thread, NULL, (void *(*)(void *)) & listen_client_connection, (void *)newsockfd);
        logger_debug("Created new thread %ld to handle this connection\n", *thread);

        LOCK(MUTEX_APPEND_LIST);
        chained_list_sockets_fd = chained_list_append_end(chained_list_sockets_fd, (void *)newsockfd);
        chained_list_threads = chained_list_append_end(chained_list_threads, (void *)thread);
        UNLOCK(MUTEX_APPEND_LIST);
    }

    return 0;
}

void sigint_handler(int _sigint)
{
    if (!received_sigint)
    {
        logger_warn("SIGINT received, closing descriptors and finishing...\n");
        cleanup(0);
        received_sigint = TRUE;
    }
    else
    {
        logger_error("Already received SIGINT... Waiting to finish cleaning up...\n");
    }
}

void handle_signals()
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);
    sigaction(SIGINT, &sigint_action, NULL); // Activating it twice works, so don't remove this ¯\_(ツ)_/¯
}

void *monitor_connection_keep_alive(void *sockfd)
{
}

int handle_server_connection(struct sockaddr_in *serv_addr)
{
    socklen_t socklen = sizeof(struct sockaddr_in);

    if (connect(ring->primary_fd, (struct sockaddr *)serv_addr, &socklen) < 0)
    {
        logger_error("When accepting connection\n");
        UNLOCK(MUTEX_RECONNECT);
        return -1;
    }

    pthread_t *listen_connection_tid;

    // Thread to communicate with server
    pthread_create(listen_connection_tid, NULL, (void *(*)(void *)) & listen_server_connection, (void *)ring->primary_fd);
    logger_debug("Created new thread %ld to handle this connection\n", *listen_connection_tid);
    // TODO add to thread list

    LOCK(MUTEX_APPEND_LIST);
    chained_list_threads = chained_list_append_end(chained_list_threads, (void *)listen_connection_tid);
    UNLOCK(MUTEX_APPEND_LIST);

    return 0;
}

void *listen_server_connection(void *sockfd)
{
    NOTIFICATION notification;
    int is_server_connected = TRUE;
    int bytes_read;

    while (is_server_connected)
    {
        bzero((void *)&notification, sizeof(NOTIFICATION));
        bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));

        if (bytes_read < 0)
        {
            logger_error("[Socket %d] When reading from socket\n", sockfd);
            is_server_connected = FALSE;
        }
        else if (bytes_read == 0)
        {
            logger_info("[Socket %d] Server closed connection\n", sockfd);
            is_server_connected = FALSE;
        }

        if (!is_server_connected)
            break;

        switch (notification.type)
        {
        case NOTIFICATION_TYPE__LOGIN:
            logger_info("Server authorized login!\n");
            process_server_message(&notification);
            break;

        case NOTIFICATION_TYPE__MESSAGE:
            logger_info("Received message from server: %s\n", notification.message);
            process_server_message(&notification);
            break;

        case NOTIFICATION_TYPE__INFO:
            logger_info("Received info from server: %s\n", notification.message);
            process_server_message(&notification);
            break;

        default:
            logger_error("Received unexpected notification %d\n", notification.type);
            break;
        }
    }
}

SERVER_RING *connect_to_leader()
{
    int sockfd = socket_create();
    SERVER_RING *ring = server_ring_initialize();

    while (TRUE)
    {
        // Sleep for some seconds before trying to find again
        sleep(1);

        ring->self_index = 0; // To go around everything
        server_ring_connect_with_next_server(ring, sockfd);

        // Finally found someone, so can ask for the leader
        if (ring->next_index != ring->self_index)
        {
            // Need to ask who is the primary
            logger_info("Connected with follower in port %d\n", ring->server_ring_ports[ring->next_index]);
            logger_info("Will try to find which is the primary port\n");

            NOTIFICATION notification = {.type = NOTIFICATION_TYPE__LEADER_QUESTION};
            int bytes_wrote = write(ring->next_sockfd, (void *)&notification, sizeof(NOTIFICATION));
            if (bytes_wrote < 0)
            {
                logger_error("Error when trying to sending message to find who is the current leader. Will retry with another ring search...\n");
                continue;
            }

            int bytes_read = read(ring->next_sockfd, (void *)&notification, sizeof(NOTIFICATION));
            if (bytes_read < 0)
            {
                logger_error("Error when trying to receive message to find who is the current leader. Will retry with another ring search...\n");
                continue;
            }
            if (notification.type != NOTIFICATION_TYPE__ELECTED)
            {
                logger_error("Error when trying to receive message to find who is the current leader. Received message of type %d. Will retry another ring search...\n", notification.type);
                continue;
            }

            ring->primary_idx = notification.data;
            logger_info("Found the primary index: %d\n", ring->primary_idx);

            close(ring->next_sockfd);

            // Connect per se with the primary
            ring->primary_fd = socket_create();
            struct sockaddr_in primary_addr;

            primary_addr.sin_family = AF_INET;
            bzero(&(primary_addr.sin_zero), 8);
            primary_addr.sin_port = htons(ring->server_ring_ports[ring->primary_idx]);
            struct hostent *in_addr = gethostbyname(ring->server_ring_addresses[ring->primary_idx]);
            primary_addr.sin_addr = *((struct in_addr *)in_addr->h_addr);

            if (connect(ring->primary_fd, (struct sockaddr *)&primary_addr, sizeof(primary_addr)) < 0)
            {
                logger_error("When trying to connect to primary server. Will retry another ring search...\n");
                continue;
            }

            return ring;
        }
    }
}

void keep_alive_with_server()
{
    pthread_t tid;
    ring->keepalive_fd = socket_create();

    struct sockaddr_in keepalive_addr;

    keepalive_addr.sin_family = AF_INET;
    bzero(&(keepalive_addr.sin_zero), 8);

    keepalive_addr.sin_port = htons(ring->server_ring_ports[ring->primary_idx]);
    struct hostent *in_addr = gethostbyname(ring->server_ring_addresses[ring->primary_idx]);
    keepalive_addr.sin_addr = *((struct in_addr *)in_addr->h_addr);

    if (connect(ring->keepalive_fd, (struct sockaddr *)&keepalive_addr, sizeof(keepalive_addr)) < 0)
    {
        logger_error("When connecting to main server, should try to reconnect\n");
        return;
    }

    while (TRUE)
    {
        logger_debug("Sending a keep alive to %d\n", ring->primary_idx);

        NOTIFICATION notification = {.type = NOTIFICATION_TYPE__KEEPALIVE}, read_notification;
        int bytes_wrote = send(ring->keepalive_fd, (void *)&notification, sizeof(NOTIFICATION), MSG_NOSIGNAL);
        if (bytes_wrote < 0)
        {
            logger_error("Error when sending keep alive. Main disconnected.\n");
            return;
        }

        int bytes_read = read(ring->keepalive_fd, (void *)&read_notification, sizeof(NOTIFICATION));
        if (bytes_read < 0)
        {
            logger_error("Error when receiving keep alive.\n");
            return;
        }

        // Sleep for some time, before checking the main again
        sleep(3);
    }
}

void *keep_server_connection(void *_)
{
    while (TRUE)
    {
        if (IS_CONNECTED_TO_SERVER)
        {
            // Keepalive with the server which is available in ring
            keep_alive_with_server();

            // The above function returns when loses connection with server, so we need to reconnect
            logger_info("Lost connection with server, warning everyone that the server is not connected anymore\n");
            IS_CONNECTED_TO_SERVER = FALSE;
        }
        else
        {
            // Reconnect with the server if we detected we are not connected anymore
            ring = connect_to_leader();

            logger_info("Connection with the main server restablished!\n");
            IS_CONNECTED_TO_SERVER = TRUE;
        }
    }

    // Assert that this is never reached
    assert(0);
}

void *listen_message_processor()
{
    while (TRUE)
    {
        LOCK(MUTEX_CONSUMER);
        logger_info("Received message from client. Processing it..");
        send_server(processing_message);
        UNLOCK(MUTEX_MESSAGE_QUEUE);
    }
}

void *listen_client_connection(void *sockfd)
{
    char username[MAX_USERNAME_LENGTH];

    NOTIFICATION notification;
    int known_user_ID = FALSE;
    int is_client_connected = TRUE;
    int bytes_read;

    while (is_client_connected)
    {
        bzero((void *)&notification, sizeof(NOTIFICATION));
        bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));

        if (bytes_read < 0)
        {
            logger_error("[Socket %d] When reading from socket\n", sockfd);
            is_client_connected = FALSE;
        }
        else if (bytes_read == 0)
        {
            logger_info("[Socket %d] Client closed connection\n", sockfd);
            is_client_connected = FALSE;
        }

        if (!is_client_connected)
            break;

        switch (notification.type)
        {
        case NOTIFICATION_TYPE__LOGIN:
            logger_info("Received login attempt from client: %s\n", notification.author);
            strcpy(username, notification.author);
            known_user_ID = TRUE;
            process_client_message(&notification);
            break;

        case NOTIFICATION_TYPE__MESSAGE:
            logger_info("Received message from client: %s\n", notification.message);
            process_client_message(&notification);
            break;

        case NOTIFICATION_TYPE__KEEPALIVE:
            answer_keep_alive();
            break;

        default:
            logger_error("Received unexpected notification\n");
            break;
        }
    }

    if (known_user_ID)
        //TODO
        disconnectUser(username);
    //TODO
    handle_socket_disconnection(sockfd);
}

void process_client_message(NOTIFICATION *notification)
{
    logger_info("Processing notification from client...\n");
    logger_info("Message from: %s\n", notification->author);

    LOCK(MUTEX_MESSAGE_QUEUE);
    processing_message = notification;
    UNLOCK(MUTEX_CONSUMER);
}

void send_server(NOTIFICATION *notification)
{
    int status;

    do
    {
        status = send_notification(notification, ring->primary_fd);
    } while (status < 0);
}

int send_notification(NOTIFICATION *notification, int sockfd)
{
    int bytes_read = write(sockfd, notification, sizeof(NOTIFICATION));
    if (bytes_read < 0)
    {
        logger_error("Failed to communicate with server...\n");
    }

    return bytes_read;
}

void cancel_thread(void *void_pthread)
{
    pthread_t thread_id = *((pthread_t *)void_pthread);

    logger_debug("Cancelling thread TID %li from thread TID %li...\n", (unsigned long int)thread_id, (unsigned long int)pthread_self());
    pthread_cancel(thread_id);
}

void close_socket(void *void_socket)
{
    int socket = *((int *)void_socket);

    logger_debug("Closing socket %d...\n", socket);
    close(socket);
}

void cleanup(int exit_code)
{
    save_savefile(user_hash_table);

    chained_list_iterate(chained_list_threads, &cancel_thread);
    chained_list_iterate(chained_list_sockets_fd, &close_socket);
    chained_list_free(chained_list_threads);
    chained_list_free(chained_list_sockets_fd);

    exit(exit_code);
}