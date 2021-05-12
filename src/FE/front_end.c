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
#include <semaphore.h>
#include <assert.h>

#include "chained_list.h"
#include "exit_errors.h"
#include "logger.h"
#include "packet.h"
#include "user.h"
#include "hash.h"
#include "notification.h"
#include "savefile.h"

#include "config.h"

#define LOCK(mutex) pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)

#define TRUE 1
#define FALSE 0

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;

int serverRM_socket;
int client_connections_socket;
int serverRM_online = FALSE;

unsigned long long GLOBAL_NOTIFICATION_ID = 0;

HASH_TABLE user_hash_table = NULL;

NOTIFICATION *processing_message;

// Semaphores
sem_t online_semaphore;
sem_t users_map_semaphore;
sem_t sockets_map_semaphore;

void handle_signals(void);
void configure_connection(int *, sockaddr_in *);
void handle_server_connection(sockaddr_in *);
void *listen_server_connection(void *);

int main(int argc, char *argv[])
{
    int i = 0;

    // Ports
    int portServerRM

    // Sockets Address Config
    socklen_t socklen = sizeof(struct sockaddr_in);
    struct sockaddr_in serv_addr, client_addr;

    pthread_t reconnect_tid;
    pthread_t message_consumer_tid;

    // Initializing Mutexes
    pthread_mutex_t serverRM_online_mutex = PTHREAD_MUTEX_INITIALIZER;


    handle_signals();

    if(argc == 2)
    {
        portServerRM = argv[1]
    } else
    {
        logger_error("Must inform a portServer");   
    }

    // Server address config
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portServerRM);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    configure_connection(&serverRM_socket, &serv_addr);

    // Block reconnection attempt
    LOCK(reconnect_mutex);

    handle_server_connection(&serv_addr);

    // Server reconnect
    pthread_create(&reconnect_tid, NULL, listen_server_reconnect, NULL);

    // Message Consumer
    pthread_create(&message_consumer_tid, NULL, listen_message_processor, NULL);

    socklen_t clilen = sizeof(struct sockaddr_in);
    struct sockaddr_in cli_addr;

    while(TRUE)
    {
        int *newsockfd = (int *)malloc(sizeof(int));

        if ((*newsockfd = accept(client_connections_socket, (struct sockaddr *)&cli_addr, &clilen)) < 0)
        {
            logger_error("When accepting client connection\n");
            cleanup(ERROR_ACCEPT);
        }

        logger_info("New Client connection from %s:%d\n", inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);

        pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
        pthread_create(thread, NULL, (void *(*)(void *)) &listen_client_connection, (void *)newsockfd);
        logger_debug("Created new thread %ld to handle this connection\n", *thread);

        // TODO listas de sockets e threads pra dar free
        // chained_list_sockets_fd = chained_list_append_end(chained_list_sockets_fd, (void *)newsockfd);
        // chained_list_threads = chained_list_append_end(chained_list_threads, (void *)thread);

        // pthread_t *keepalive_client_tid = (pthread_t *)malloc(sizeof(pthread_t));
        // pthread_create(keepalive_client_tid, NULL, monitorConnectionKeepAlive, (void *) args2);
        // logger_debug("Created new thread %ld to monitor Keep Alive signal\n", *thread);
    }

    return 0;
}

void handle_signals()
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);
    sigaction(SIGINT, &sigint_action, NULL); // Activating it twice works, so don't remove this ¯\_(ツ)_/¯
}

void configure_connection(int *sockfd, sockaddr_in *serv_addr)
{
    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("When opening socket\n");
        exit(ERROR_OPEN_SOCKET);
    }

    int this_true = 1;
    if (setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, &this_true, sizeof(int)) == -1)
    {
        logger_error("When setting the socket configurations\n");
        exit(ERROR_CONFIGURATION_SOCKET);
    }

    if (bind(*sockfd, (struct sockaddr *) serv_addr, sizeof(*serv_addr)) < 0)
    {
        logger_error("When binding socket\n");
        exit(ERROR_BINDING_SOCKET);
    }

    if (listen(*sockfd, CONNECTIONS_TO_ACCEPT) < 0)
    {
        logger_error("When starting to listen");
        cleanup(ERROR_LISTEN);
    }
    logger_info("Listening on port %d...\n", server_port);
}

int handle_server_connection(sockaddr_in *serv_addr)
{
    if (connect(serverRM_socket, (struct sockaddr *)serv_addr, &socklen) < 0)
    {
        logger_error("When accepting connection\n");
        UNLOCK(reconnect_mutex);
        return -1;
    }

    pthread_t *listen_connection_tid;
    pthread_t *keepalive_server_tid;

    // Thread to communicate with server
    pthread_create(listen_connection_tid, NULL, (void *(*)(void *)) &listen_server_connection, (void *)newsockfd);
    logger_debug("Created new thread %ld to handle this connection\n", *listenConnection_tid);
    // TODO add to thread list

    // Thread to monitor keep alive signal
    pthread_create(keepalive_server_tid, NULL, (void *(*)(void *)) &monitor_connection_keep_alive, (void *)newsockfd);
    logger_debug("Created new thread %ld to handle the monitor keep alive\n", *keepalive_server_tid);
    // TODO add to thread list

    LOCK(serverRM_online_mutex);
    serverRM_online = TRUE;
    UNLOCK(serverRM_online_mutex);

    return 0;
}

void *listen_server_connection(void *sockfd)
{
    NOTIFICATION notification;
    int is_server_connected = TRUE;
    int bytes_read;

    // TODO connection keeper

    while(is_server_connected)
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

        case NOTIFICATION_TYPE__KEEPALIVE:
            logger_info("Received keep alive signal. Answering it...\n", sockfd);
            answer_keep_alive();
            break;

        default:
            logger_error("Received unexpected notification\n");
            break;
        }
    }
}

void *listen_server_reconnect()
{
    while (true)
    {
        LOCK(reconnect_mutex);
        logger_info("Waiting for server reconnect...");
        handleServerConnection();
    }

    return NULL;
}

void *listen_message_processor()
{
    while(true)
    {
        LOCK(message_consumer_mutex);
        logger_info("Received message from client. Processing it..");
        send_server(processing_message);
        UNLOCK(message_queue_mutex);
    }
}

void *listen_client_connection(void *sockfd)
{
    char username[MAX_USERNAME_LENGTH];

    NOTIFICATION notification;
    int known_user_ID = FALSE;
    int is_client_connected = TRUE;

    while(is_client_connected)
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
            strcpy(username, notification.author)
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

    LOCK(message_queue_mutex);
    processing_message = message;
    UNLOCK(message_consumer_mutex);
}

void send_server(NOTIFICATION *notification)
{
    int hasFailed = FALSE;
    int status;

    do
    {
        status = send_notification(notification, serverRM_socket);

        if (status < 0)
        {
            logger_error("Failed to send notification. Server is off...\n");
            failed = TRUE;
            sleep(1);
        }
    }
    while (status < 0)

    if (hasFailed)
    {
        logger_info("New server connected and received message!\n");
    }
}

int send_notification(NOTIFICATION *notification, int sockfd)
{
    int bytes_read = write(socketfd, notification, sizeof(NOTIFICATION));
    if (bytes_read < 0)
    {
        logger_error("Failed to write to socket: %s\n", sockfd);
    }

    return 0;
}

void cleanup(int exit_code)
{
    save_savefile(user_hash_table);

    chained_list_iterate(chained_list_threads, &cancel_thread);
    chained_list_iterate(chained_list_sockets_fd, &close_socket);
    chained_list_free(chained_list_threads);
    chained_list_free(chained_list_sockets_fd);

    close(sockfd);

    exit(exit_code);
}