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
#include "server_ring.h"
#include "socket.h"
#include "front_end.h"

#define LOCK(mutex) pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)

#define TRUE 1
#define FALSE 0

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;
CHAINED_LIST *chained_list_messages = NULL;

static int received_sigint = FALSE;

int serverRM_socket;
int serverRM_keepalive_socket;
int serverRM_online = FALSE;
int sockfd = 0;

pthread_t reconnect_tid;
pthread_t message_consumer_tid;

unsigned long long GLOBAL_NOTIFICATION_ID = 0;

HASH_TABLE user_hash_table = NULL;

// Mutexes
pthread_mutex_t MUTEX_RECONNECT = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_ONLINE_SERVER = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_CONSUMER = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_MESSAGE_QUEUE = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_APPEND_LIST = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_LOGIN = PTHREAD_MUTEX_INITIALIZER;

void cancel_thread(void *);
void close_socket(void *);
void sigint_handler(int);
void handle_signals(void);
int handle_server_connection(struct sockaddr_in *);
void *listen_server_connection(void *);
void *keep_server_connection(void *);
void keep_alive_with_server(void);
void *listen_message_processor(void *);
void *listen_client_connection(void *);
void send_server(NOTIFICATION *);
int send_notification(NOTIFICATION *, int);
void cleanup(int);

SERVER_RING *ring;
int IS_CONNECTED_TO_SERVER = FALSE;

int front_end_port_idx = 0;

int main(int argc, char *argv[])
{
    // Sockets Address Config
    struct sockaddr_in serv_addr, client_addr;
    int port = 0;

    // Configure user hash table
    user_hash_table = hash_init();

    // Variables used below to connect to the incoming client connections
    socklen_t clilen = sizeof(struct sockaddr_in);

    handle_signals();

    // Server reconnect is responsible to keep the connection to the RM
    pthread_create(&reconnect_tid, NULL, (void *(*)(void *)) & keep_server_connection, NULL);

    // Creating this socket
    sockfd = socket_create();

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    for (front_end_port_idx = 0; front_end_port_idx < (NUMBER_OF_FES) + 1; front_end_port_idx++)
    {
        if (front_end_port_idx == NUMBER_OF_FES)
        {
            // We have no more FEs to connect to
            logger_error("When trying to bind to the available FE ports, couldn't find any assignable open port\n");
            exit(ERROR_BINDING_SOCKET);
        }

        port = FE_PORTS[front_end_port_idx];
        serv_addr.sin_port = htons(port);
        struct hostent *in_addr = gethostbyname(FE_HOSTS[front_end_port_idx]);
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

    logger_info("Listening on port %d...\n", port);

    // Incoming message listener ---> está dando seg fault do nada
    pthread_create(&message_consumer_tid, NULL, (void *(*)(void *)) & listen_message_processor, NULL);

    while (TRUE)
    {
        int *newsockfd = (int *)malloc(sizeof(int));
        *newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, &clilen);

        if (*newsockfd == -1)
        {
            logger_error("When accepting client connection\n");
            cleanup(ERROR_ACCEPT);
        }

        logger_info("New Client connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);

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

int handle_server_connection(struct sockaddr_in *serv_addr)
{
    socklen_t socklen = sizeof(*serv_addr);

    if (connect(ring->primary_fd, (struct sockaddr *)serv_addr, socklen) < 0)
    {
        logger_error("When accepting connection\n");
        UNLOCK(MUTEX_RECONNECT);
        return -1;
    }

    pthread_t listen_connection_tid;

    // Thread to communicate with server
    pthread_create(&listen_connection_tid, NULL, (void *(*)(void *)) & listen_server_connection, (void *)&ring->primary_fd);
    logger_debug("Created new thread %ld to handle this connection\n", listen_connection_tid);
    // TODO add to thread list

    LOCK(MUTEX_APPEND_LIST);
    chained_list_threads = chained_list_append_end(chained_list_threads, (void *)listen_connection_tid);
    UNLOCK(MUTEX_APPEND_LIST);

    return 0;
}

void *listen_server_connection(void *void_sockfd)
{
    NOTIFICATION notification;
    int bytes_read;

    while (1)
    {
        if (!IS_CONNECTED_TO_SERVER)
        {
            sleep(1);
            continue;
        }

        bzero((void *)&notification, sizeof(NOTIFICATION));
        bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));

        if (bytes_read < 0)
        {
            logger_error("[Socket %d] When reading from socket\n", sockfd);
            continue;
        }
        else if (bytes_read == 0)
        {
            logger_info("[Socket %d] Server closed connection\n", sockfd);
            continue;
        }

        if (notification.type != NOTIFICATION_TYPE__MESSAGE && notification.type != NOTIFICATION_TYPE__INFO)
        {
            logger_warn("Received unexpected notification %d from server. Will just ignore it\n", notification.type);
            continue;
        }

        // Send back notification to the connected users
        HASH_NODE *node = hash_find(user_hash_table, notification.receiver);
        USER *user = (USER *)node->value;
        for (int i = 0; i < MAX_SESSIONS; i++)
        {
            int socket_fd = user->sockets_fd[i];
            if (socket_fd != -1)
            {
                if (write(socket_fd, &notification, sizeof(NOTIFICATION)) < 0)
                    logger_error("When sending notification %d to %s through socket %d\n", notification.id, user->username, socket_fd);
                else
                    logger_info("Sent notification %d with message '%s' to %s on socket %d\n", notification.id, notification.message, notification.receiver, socket_fd);
            }
        }
    }

    return NULL;
}

SERVER_RING *connect_to_leader()
{
    int sockfd = socket_create();
    SERVER_RING *ring = server_ring_initialize();

    while (TRUE)
    {
        // Sleep for some seconds before trying to find again
        sleep(1);

        ring->self_index = -5; // To not collision inside the next function and to go around everything
        server_ring_connect_with_next_server(ring, sockfd);

        // Finally found someone, so can ask for the leader
        if (ring->next_index != ring->self_index)
        {
            // Need to ask who is the primary
            logger_info("Connected with follower in port %d\n", ring->server_ring_ports[ring->next_index]);
            logger_info("Will try to find which is the primary port\n");

            NOTIFICATION notification = {.type = NOTIFICATION_TYPE__LEADER_QUESTION};
            int bytes_wrote = write(sockfd, (void *)&notification, sizeof(NOTIFICATION));
            if (bytes_wrote < 0)
            {
                logger_error("Error when trying to sending message to find who is the current leader. Will retry with another ring search...\n");
                continue;
            }

            int bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));
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

            close(sockfd);

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

            NOTIFICATION connect_notification = {.type = NOTIFICATION_TYPE__FE_CONNECTION, .data = front_end_port_idx};
            bytes_wrote = write(ring->primary_fd, (void *)&connect_notification, sizeof(NOTIFICATION));
            if (bytes_wrote < 0)
            {
                logger_error("Error when trying to connect to primary server. Will retry with another ring search...\n");
                continue;
            }

            return ring;
        }

        logger_info("Could not find new leader. Going again in a few...\n");
    }
}

void keep_alive_with_server()
{
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
            logger_info("Not connected. Will try to connect to the leader\n");
            ring = connect_to_leader();

            logger_info("Connection with the main server restablished!\n");
            IS_CONNECTED_TO_SERVER = TRUE;
        }
    }

    // Assert that this is never reached
    assert(0);
}

void *listen_message_processor(void *_)
{
    while (TRUE)
    {
        LOCK(MUTEX_MESSAGE_QUEUE);
        if (chained_list_messages)
        {
            logger_info("Received message from client. Processing it..\n");
            send_server((NOTIFICATION *)chained_list_messages->val);
            chained_list_messages = chained_list_messages->next;
        }
        UNLOCK(MUTEX_MESSAGE_QUEUE);

        // Give control back to another threads for some time
        usleep(5);
    }
}

int get_free_socket_spot(int *sockets_fd)
{
    int i;
    for (i = 0; i <= MAX_SESSIONS; i++)
    {
        if (sockets_fd[i] == -1)
            return i;
    }

    return -1;
}

USER *login_user(int sockfd)
{
    char username[MAX_USERNAME_LENGTH];

    int bytes_read = read(sockfd, (void *)username, sizeof(char) * MAX_USERNAME_LENGTH);
    if (bytes_read < 0)
    {
        logger_error("Couldn't read username for socket %d\n", sockfd);
        return NULL;
    }

    // Only one user can be logged in each time
    LOCK(MUTEX_LOGIN);

    HASH_NODE *hash_node = hash_find(user_hash_table, username);
    if (hash_node == NULL)
    {
        logger_info("New user logged: %s\n", username);
        USER *user = init_user();

        strcpy(user->username, username);
        user->sockets_fd[0] = sockfd;
        user->sessions_number = 1;

        hash_node = hash_insert(user_hash_table, username, (void *)user);

        // Need to unlock here because of early return
        UNLOCK(MUTEX_LOGIN);
        return user;
    }

    USER *user = (USER *)hash_node->value;

    // Do not allow to play around with user while logging a new user
    LOCK(user->mutex);
    if (user->sessions_number < MAX_SESSIONS)
    {
        int free_socket_spot = get_free_socket_spot(user->sockets_fd);
        logger_info("User in another session: %s\n", user->username);
        logger_info("Total sessions: %d\n", user->sessions_number);
        logger_info("Saving socket in position: %d\n", free_socket_spot);
        user->sockets_fd[free_socket_spot] = sockfd;
        user->sessions_number++;

        // Need to duplicate because of early return
        UNLOCK(user->mutex);
        UNLOCK(MUTEX_LOGIN);

        return user;
    }

    UNLOCK(user->mutex);
    UNLOCK(MUTEX_LOGIN);

    return NULL;
}

void *listen_client_connection(void *void_sockfd)
{
    NOTIFICATION notification; // Used to receive the notifications
    int bytes_read, sockfd = *((int *)void_sockfd);

    USER *current_user = login_user(sockfd);
    int can_login = current_user != NULL;

    bytes_read = write(sockfd, &can_login, sizeof(can_login));
    if (bytes_read < 0)
    {
        logger_error("[Socket %d] When sending login ACK/NACK (%d)\n", sockfd, can_login);
        return NULL;
    }
    if (!can_login)
    {
        logger_error("User couldn't login! Max connections (%d) reached\n", MAX_SESSIONS);
        return NULL;
    }

    // Tell server that this guy logged in
    NOTIFICATION user_login = {.type = NOTIFICATION_TYPE__LOGIN};
    strcpy(user_login.author, current_user->username);
    send_server(&user_login);

    // Keep receiving messages from the client, and sending them to the server
    while (1)
    {
        bzero((void *)&notification, sizeof(NOTIFICATION));

        /* read from the socket */
        bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));
        if (bytes_read < 0)
        {
            logger_error("[Socket %d] When reading from socket\n", sockfd);
        }
        else if (bytes_read == 0)
        {
            logger_info("[Socket %d] Client closed connection\n", sockfd);

            // Lock user while playing around with sockets list
            LOCK(current_user->mutex);
            current_user->sessions_number--;
            for (int i = 0; i < MAX_SESSIONS; i++)
                if (current_user->sockets_fd[i] == sockfd)
                {
                    logger_info("[Socket %d] Freed %d socket position\n", sockfd, i);
                    current_user->sockets_fd[i] = -1;
                    break;
                }

            UNLOCK(current_user->mutex);

            // Tell server about this logout
            NOTIFICATION user_logout = {.type = NOTIFICATION_TYPE__LOGOUT};
            strcpy(user_logout.author, current_user->username);
            send_server(&user_logout);

            return NULL;
        }
        else
        {
            logger_info("[Socket %d] Received message with type %d from client (%s), adding to processing queue\n", sockfd, notification.type, notification.message);

            NOTIFICATION *notification_copy = (NOTIFICATION *)calloc(1, sizeof(NOTIFICATION));
            memcpy(notification_copy, &notification, sizeof(NOTIFICATION));

            LOCK(MUTEX_MESSAGE_QUEUE);
            chained_list_messages = chained_list_append_end(chained_list_messages, (void *)notification_copy);
            UNLOCK(MUTEX_MESSAGE_QUEUE);
        }
    };

    return NULL;
}

void send_server(NOTIFICATION *notification)
{
    int status;

    do
    {
        status = write(ring->primary_fd, notification, sizeof(NOTIFICATION));

        if (status < 0)
        {
            logger_info("Failed to send message %d to server. Will retry in a few...\n", notification->id);
            sleep(1);
        }
        else
        {
            logger_info("Sent %s (%d) message to server\n", notification->message, notification->id);
        }
    } while (status < 0);
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
    chained_list_iterate(chained_list_threads, &cancel_thread);
    chained_list_iterate(chained_list_sockets_fd, &close_socket);
    chained_list_free(chained_list_threads);
    chained_list_free(chained_list_sockets_fd);

    exit(exit_code);
}
