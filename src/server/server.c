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
#include "front_end.h"

typedef int boolean;
#define FALSE 0
#define TRUE 1

extern int errno;

static int received_sigint = FALSE;

void *handle_connection(void *);
void handle_connection_login(int, NOTIFICATION *);
void handle_connection_leader_question(int);
void handle_connection_keepalive(int, NOTIFICATION *);
void handle_connection_election(NOTIFICATION *, int sockfd);
void handle_connection_elected(NOTIFICATION *);
void handle_connection_fe(int, NOTIFICATION *);
void close_socket(void *);
void cancel_thread(void *);
void sigint_handler(int);
void handle_signals(void);
void *handle_eof(void *);
void cleanup(int);
USER *login_user(char *);
USER *logout_user(char *);
void process_message(NOTIFICATION *, USER *);
void receive_message(NOTIFICATION *, USER *);
void follow_user(NOTIFICATION *, USER *);
void print_username(void *);
void send_message(NOTIFICATION *);
void send_initial_replication(int);
void send_replication(NOTIFICATION *);
void handle_replication(NOTIFICATION *);
void handle_pending_notifications(USER *current_user, int sockfd, int send);
void send_pending_notifications(USER *current_user, int sockfd);

// TODO: remove this function later after debugs
void print_notification(NOTIFICATION *notification);

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;
int sockfd = 0;

SERVER_RING *server_ring = NULL;

HASH_TABLE user_hash_table = NULL;

int FE_SOCKFDS[NUMBER_OF_FES];

// MUTEXES
pthread_mutex_t MUTEX_LOGIN = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_FOLLOW = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_PENDING_NOTIFICATIONS = PTHREAD_MUTEX_INITIALIZER;

#define LOCK(mutex) pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)

unsigned long long GLOBAL_NOTIFICATION_ID = 0;

typedef struct
{
    NOTIFICATION *notification;
    USER *user;
} MESSAGE_TO_PROCESS;

int main(int argc, char *argv[])
{

    logger_debug("Initializing on debug mode!\n");

    handle_signals();

    server_ring = server_ring_initialize();
    server_ring_connect(server_ring);

    socklen_t clilen = sizeof(struct sockaddr_in);
    struct sockaddr_in cli_addr;

    user_hash_table = hash_init();

    // This loop is responsible for keeping accepting new connections from clients
    while (1)
    {
        int *newsockfd = (int *)malloc(sizeof(int));
        *newsockfd = accept(server_ring->self_sockfd, (struct sockaddr *)&cli_addr, &clilen);

        if (*newsockfd == -1)
        {
            logger_error("When accepting connection\n");
            cleanup(ERROR_ACCEPT);
        }

        logger_info("New connection from %s:%d\n", inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);

        pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
        pthread_create(thread, NULL, (void *(*)(void *)) & handle_connection, (void *)newsockfd);
        logger_debug("Created new thread %ld to handle this connection\n", *thread);

        chained_list_sockets_fd = chained_list_append_end(chained_list_sockets_fd, (void *)newsockfd);
        chained_list_threads = chained_list_append_end(chained_list_threads, (void *)thread);
    }

    // Assert that this is never reached
    assert(0);
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

USER *login_user(char *username)
{
    logger_info("Attempting to log user: %s\n", username);

    // Only one user can be logged in each time
    LOCK(MUTEX_LOGIN);

    HASH_NODE *hash_node = hash_find(user_hash_table, username);
    if (hash_node == NULL)
    {
        logger_info("User didn't existed, just created it\n");
        USER *user = init_user();

        strcpy(user->username, username);
        user->sessions_number = 1;

        hash_node = hash_insert(user_hash_table, username, (void *)user);
        logger_debug("Created user on %p\n", hash_node);
        hash_print(user_hash_table);

        // Need to unlock here because of early return
        UNLOCK(MUTEX_LOGIN);

        return user;
    }
    UNLOCK(MUTEX_LOGIN);

    // User already existed, will just increment the session number
    USER *user = (USER *)hash_node->value;
    user->sessions_number++;

    return user;
}

USER *logout_user(char *username)
{
    HASH_NODE *hash_node = hash_find(user_hash_table, username);
    USER *user = (USER *)hash_node->value;
    user->sessions_number--;

    return user;
}

void print_username(void *void_parameter)
{
    char *parameter = ((char *)void_parameter);
    printf("%s", parameter);
}

void follow_user(NOTIFICATION *follow_notification, USER *current_user)
{
    char *user_to_follow_username = strdup(follow_notification->message);
    strcpy(follow_notification->receiver, current_user->username);
    strcpy(follow_notification->target, follow_notification->message);
    print_notification(follow_notification);

    if (strcmp(user_to_follow_username, current_user->username) == 0)
    {
        logger_warn("User tried following itself, won't work!\n");

        NOTIFICATION notification = {
            .command = (COMMAND)NULL,
            .id = GLOBAL_NOTIFICATION_ID++,
            .timestamp = time(NULL),
            .message = "User tried following itself. This is not allowed.",
            .type = NOTIFICATION_TYPE__INFO};
        strcpy(notification.receiver, current_user->username);

        send_message(&notification);

        return;
    }

    // Only allow one follow to be processed at each given time
    LOCK(MUTEX_FOLLOW);

    HASH_NODE *followed_user_node = hash_find(user_hash_table, user_to_follow_username);
    if (followed_user_node == NULL)
    {
        char *error_message = (char *)calloc(220, sizeof(char));
        sprintf(error_message, "Could not follow the user %s. It doesn't exist\n", user_to_follow_username);

        NOTIFICATION notification = {
            .command = (COMMAND)NULL,
            .id = GLOBAL_NOTIFICATION_ID++,
            .timestamp = time(NULL),
            .type = NOTIFICATION_TYPE__INFO};
        strcpy(notification.receiver, current_user->username);
        strcpy(notification.message, error_message);

        send_message(&notification);

        logger_error(error_message);
    }
    else
    {
        USER *user = (USER *)followed_user_node->value;
        char *dup_current_user_username = strdup(current_user->username);

        // Lock because we are possibly going to play around with follow list
        LOCK(user->mutex);

        // In a real life, this probably should be a trie or any other tree-like structure
        // with O(logn) worst case, and not this O(n) solution
        char *list_current_user_username = (char *)chained_list_find(
            user->followers,
            dup_current_user_username,
            (int (*)(void *, void *))strcmp);

        if (list_current_user_username == NULL)
        {
            // Execution:
            user->followers = chained_list_append_end(user->followers, dup_current_user_username);
            logger_debug("New list of followers: ");
            chained_list_print(user->followers, &print_username);

            char *info_message = (char *)calloc(220, sizeof(char));
            sprintf(info_message, "The user '%s' was followed!", user_to_follow_username);
            strcpy(follow_notification->message, info_message);

            // Sending for agreement and response will go after every replication
            if (server_ring->is_primary)
                send_replication(follow_notification);
        }
        else
        {
            char *error_message = (char *)calloc(220, sizeof(char));
            sprintf(error_message, "The user '%s' already follows '%s'", current_user->username, user_to_follow_username);

            NOTIFICATION notification = {
                .command = (COMMAND)NULL,
                .id = GLOBAL_NOTIFICATION_ID++,
                .timestamp = time(NULL),
                .type = NOTIFICATION_TYPE__INFO};
            strcpy(notification.message, error_message);
            strcpy(notification.receiver, current_user->username);

            send_message(&notification);

            logger_error("%s\n", error_message);
        }

        UNLOCK(user->mutex);
    }

    UNLOCK(MUTEX_FOLLOW);
}

void process_message(NOTIFICATION *notification, USER *user)
{
    if (notification->command == FOLLOW)
    {
        logger_info("Following user: %s\n", notification->message);
        follow_user(notification, user);
    }
    else if (notification->command == SEND)
    {
        logger_info("Received message: %s\n", notification->message);
        receive_message(notification, user);
    }
}

void receive_message(NOTIFICATION *receive_notification, USER *current_user)
{
    NOTIFICATION *notification = (NOTIFICATION *)calloc(1, sizeof(NOTIFICATION));
    strcpy(notification->author, current_user->username);
    strcpy(notification->receiver, current_user->username);
    strcpy(notification->message, receive_notification->message);
    notification->id = GLOBAL_NOTIFICATION_ID++;
    notification->timestamp = receive_notification->timestamp;
    notification->type = NOTIFICATION_TYPE__MESSAGE;

    LOCK(MUTEX_FOLLOW);
    CHAINED_LIST *follower = current_user->followers;
    send_message(notification);
    while (follower)
    {
        strcpy(notification->receiver, (char *)follower->val);
        send_message(notification);
        follower = follower->next;
    }
    UNLOCK(MUTEX_FOLLOW);

    // Lock user to update list of notification
    LOCK(current_user->mutex);
    current_user->notifications = chained_list_append_end(current_user->notifications, (void *)notification);
    UNLOCK(current_user->mutex);
}

// Sends a NOTIFICATION to a user
void send_message(NOTIFICATION *notification)
{

    HASH_NODE *node = hash_find(user_hash_table, notification->receiver);
    if (!node)
    {
        logger_error("When sending message to non existent username %s\n", notification->receiver);
        return;
    }

    // Gets the user and lock its mutex
    USER *user = (USER *)node->value;
    LOCK(user->mutex);

    if (user->sessions_number > 0)
    {
        int user_hash = hash_address(user->username) % (NUMBER_OF_FES);
        int socket_fd = FE_SOCKFDS[user_hash];

        if (write(socket_fd, notification, sizeof(NOTIFICATION)) < 0)
            logger_error("When sending notification %d to %s through socket %d\n", notification->id, user->username, socket_fd);
        else
            logger_info("Sent notification %d with message '%s' to %s 's FE on socket %d\n", notification->id, notification->message, notification->receiver, socket_fd);
    }
    else
    {
        logger_info("Added notification %ld with message '%s' to be sent later to %s\n", notification->id, notification->message, user->username);
        // Add to the notifications which must be sent to this user later on
        LOCK(MUTEX_PENDING_NOTIFICATIONS);
        user->pending_notifications = chained_list_append_end(user->pending_notifications, (void *)notification);
        UNLOCK(MUTEX_PENDING_NOTIFICATIONS);
    }

    UNLOCK(user->mutex);
}

void *handle_connection(void *void_sockfd)
{
    int sockfd = *((int *)void_sockfd);

    NOTIFICATION notification;
    int bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));
    if (bytes_read < 0)
    {
        logger_error("Couldn't read first notification from socket %d\n", sockfd);
        return NULL;
    }

    switch (notification.type)
    {
    case NOTIFICATION_TYPE__FE_CONNECTION:
        if (server_ring->is_primary)
        {
            logger_info("[Socket %d] Received connection with FE_CONNECTION type.\n", sockfd);
            handle_connection_fe(sockfd, &notification);
            break;
        }
        logger_warn("[Socket %d] It is not primary, should not receive connection with LOGIN type\n", sockfd);
        break;
    case NOTIFICATION_TYPE__LEADER_QUESTION:
        logger_info("[Socket %d] Received connection with LEADER_QUESTION type\n", sockfd);
        handle_connection_leader_question(sockfd);
        break;
    case NOTIFICATION_TYPE__KEEPALIVE:
        logger_info("[Socket %d] Received connection with KEEP_ALIVE type\n", sockfd);
        if (!server_ring->is_primary)
        {
            logger_warn("[Socket %d] It is not primary, should not receive keep alive\n", sockfd);
            break;
        }

        handle_connection_keepalive(sockfd, &notification);
        break;
    case NOTIFICATION_TYPE__ELECTION:
        logger_info("[Socket %d] Received connection with ELECTION type\n", sockfd);
        handle_connection_election(&notification, sockfd);
        break;
    case NOTIFICATION_TYPE__ELECTED:
        logger_info("[Socket %d] Received connection with ELECTED type\n", sockfd);
        handle_connection_elected(&notification);
        break;
    case NOTIFICATION_TYPE__REPLICATION:
        logger_info("[Socket %d] Received connection with REPLICATION type\n", sockfd);
        handle_replication(&notification);
        break;
    default:
        logger_info("[Socket %d] Unhandable connection with %d type\n", sockfd, notification.type);
        break;
    }

    close(sockfd);

    return NULL;
}

void send_pending_notifications(USER *current_user, int sockfd)
{
    CHAINED_LIST *pending_notification = current_user->pending_notifications;
    while (pending_notification)
    {
        NOTIFICATION *notification = (NOTIFICATION *)pending_notification->val;
        strcpy(notification->receiver, current_user->username);

        logger_info("[Socket %d] Sending back pending notification from %d to %d\n", sockfd, notification->author, current_user->username);
        send_message(notification);

        pending_notification = pending_notification->next;
    }
}

void handle_pending_notifications(USER *current_user, int sockfd, int send)
{
    // Do not allow to add any new notification while we haven't sent the others
    LOCK(MUTEX_PENDING_NOTIFICATIONS);
    if (send)
    {

        send_pending_notifications(current_user, sockfd);
    }
    // We have sent them all, so we can clean it
    current_user->pending_notifications = NULL;
    UNLOCK(MUTEX_PENDING_NOTIFICATIONS);
}

void handle_connection_login(int sockfd, NOTIFICATION *notification)
{
    USER *current_user = login_user(notification->author);

    handle_pending_notifications(current_user, sockfd, server_ring->is_primary);

    if (server_ring->is_primary)
    {
        logger_debug("Sending LOGIN replication.");
        send_replication(notification);
    }
}

void handle_connection_leader_question(int sockfd)
{
    NOTIFICATION notification = {.type = NOTIFICATION_TYPE__ELECTED, .data = server_ring->primary_idx};
    int bytes_read = write(sockfd, &notification, sizeof(NOTIFICATION));
    if (bytes_read < 0)
        logger_error("[Socket %d] When sending primary idx (%d) back on request\n", sockfd, server_ring->primary_idx);
}

void print_notification(NOTIFICATION *notification)
{
    logger_debug("NOTIFICATION {author: %s, command: %d, data: %d, id: %d, message: %s, receiver: %s, target: %s, timestamp: %d, type: %d}\n",
                 notification->author,
                 notification->command,
                 notification->data,
                 notification->id,
                 notification->message,
                 notification->receiver,
                 notification->target,
                 notification->timestamp,
                 notification->type);
}

void handle_replication(NOTIFICATION *notification)
{
    logger_debug("🗃️🗃️🗃️ REPLICATION!");
    print_notification(notification);

    if (notification->command == LOGIN)
    {
        if (!server_ring->is_primary)
        {
            handle_connection_login(0, notification);
            send_replication(notification);
        }
        else
        {
            logger_debug("Primary received back replication LOGIN. Ignoring...\n");
        }
    }
    else if (notification->command == LOGOUT)
    {
        if (!server_ring->is_primary)
            logout_user(notification->author);
        else
            logger_debug("Primary received back replication LOGOUT. Ignoring...\n");
    }
    else
    {

        if (notification->command == FOLLOW)
        {
            if (notification->data == 1)
            {
                // Gets the user and lock its mutex
                HASH_NODE *node = hash_find(user_hash_table, notification->target);
                USER *user = (USER *)node->value;

                LOCK(user->mutex); // Because we will modify lists
                char *dup_current_user_username = strdup(notification->author);
                user->followers = chained_list_append_end(user->followers, dup_current_user_username);
                UNLOCK(user->mutex);

                logger_debug("New list of followers: ");
                chained_list_print(user->followers, &print_username);
                logger_info("Updated follow state");

                send_replication(notification);
            }
            if (notification->data == 0)
            {
                // Response after Agreement:
                NOTIFICATION response = {
                    .command = (COMMAND)NULL,
                    .id = GLOBAL_NOTIFICATION_ID++,
                    .timestamp = time(NULL),
                    .type = NOTIFICATION_TYPE__INFO,
                };
                strcpy(response.message, notification->message);
                strcpy(response.receiver, notification->receiver);
                send_message(&response);
            }
        }
        if (notification->command == SEND)
        {
            if (notification->data == 1)
            {
                // Gets the user and lock its mutex
                HASH_NODE *node = hash_find(user_hash_table, notification->receiver);
                USER *user = (USER *)node->value;

                LOCK(user->mutex);
                LOCK(MUTEX_PENDING_NOTIFICATIONS);
                logger_info("Added notification %ld with message '%s' to be sent later to %s\n", notification->id, notification->message, user->username);
                user->pending_notifications = chained_list_append_end(user->pending_notifications, (void *)notification);
                UNLOCK(MUTEX_PENDING_NOTIFICATIONS);
                UNLOCK(user->mutex);
                send_replication(notification);
            }
        }
    }
}

void send_initial_replication(int sockfd)
{
    int list_idx;
    HASH_NODE *node;
    if (user_hash_table)
    {
        for (int table_idx = 0; table_idx < HASH_SIZE; table_idx++)
        {
            for (node = user_hash_table[table_idx], list_idx = 0; node; node = node->next, list_idx++)
            {
                USER *user = (USER *)node->value;

                NOTIFICATION login_notification = {
                    .type = NOTIFICATION_TYPE__REPLICATION,
                    .command = LOGIN,
                    .data = 0,
                    .id = GLOBAL_NOTIFICATION_ID++,
                    .timestamp = time(NULL),
                };
                strcpy(login_notification.author, user->username);
                logger_debug("REPLICATION: LOGIN user %d", user->username);
                send_replication(&login_notification);

                CHAINED_LIST *pending_notification = user->pending_notifications;
                while (pending_notification)
                {
                    NOTIFICATION *notification = (NOTIFICATION *)pending_notification->val;

                    logger_debug("[Socket %d] REPLICATION: Pending notification from %d to %d\n", sockfd, notification->author, user->username);
                    send_replication(notification);

                    pending_notification = pending_notification->next;
                }
            }
        }
    }
}

void send_replication(NOTIFICATION *original)
{
    int keep_replicating = 1;
    // Creating and configuring sockfd for the keepalive
    int sockfd = socket_create();
    server_ring_connect_with_next_server(server_ring, sockfd);

    if (server_ring->server_ring_ports[server_ring->primary_idx] == server_ring->server_ring_ports[server_ring->next_index])
    {
        logger_debug("Should stop replication on next connection");
        keep_replicating = 0;
    }

    NOTIFICATION notification = {
        .type = NOTIFICATION_TYPE__REPLICATION,
        .command = original->command,
        .data = keep_replicating,
        .id = original->id,
        .timestamp = original->timestamp,
    };
    strcpy(notification.author, original->author);
    strcpy(notification.message, original->message);
    strcpy(notification.receiver, original->receiver);
    strcpy(notification.target, original->target);

    int bytes_wrote = write(sockfd, (void *)&notification, sizeof(NOTIFICATION));
    if (bytes_wrote < 0)
    {
        logger_error("Error when trying to send next replication message.\n");
        exit(ERROR_REPLICATING);
    }

    close(sockfd);
}

void handle_connection_election(NOTIFICATION *notification, int origin_sockfd)
{
    // Cancel my keep alive, because we are in an election
    pthread_cancel(server_ring->keepalive_tid);

    // Check if someone is late to the party, and inconsistent, thinking that we don't have a leader, but we actually do have
    if (server_ring->is_primary)
    {
        logger_warn("I'm already primary, but someone doesn't know, telling them\n");

        NOTIFICATION notification = {.type = NOTIFICATION_TYPE__ELECTED, .data = server_ring->self_index};
        int bytes_wrote = write(origin_sockfd, (void *)&notification, sizeof(NOTIFICATION));
        if (bytes_wrote < 0)
        {
            logger_error("Error when trying to send I was elected. Be careful, the node %d may be inoperant.\n");
        }

        return;
    }

    NOTIFICATION_TYPE notification_type = 0;
    int next_data = 0;
    LOCK(server_ring->MUTEX_ELECTION);
    if (notification->data < server_ring->self_index && server_ring->in_election) // Already participating with bigger number, just ignore it
    {
        logger_info("Already participating in the election, with a bigger number, ignoring it...\n");
        UNLOCK(server_ring->MUTEX_ELECTION);
        return;
    }
    UNLOCK(server_ring->MUTEX_ELECTION);

    LOCK(server_ring->MUTEX_ELECTION);
    server_ring->in_election = 1; // Enter in the election
    UNLOCK(server_ring->MUTEX_ELECTION);

    if (notification->data == server_ring->self_index)
    {
        // I am elected!
        logger_info("I'm the new leader! 👑\n");

        notification_type = NOTIFICATION_TYPE__ELECTED;
        next_data = server_ring->self_index;

        // Become the primary right now
        server_ring->is_primary = 1;
    }
    else
    {
        // Send message to the next one, with either my number if I am more important, or keep passing
        notification_type = NOTIFICATION_TYPE__ELECTION;
        next_data = server_ring->self_index > notification->data ? server_ring->self_index : notification->data;
        logger_info("I'm not the new leader\n");
        logger_info("Sending ahead, from idx %d that the new leader should be the %d\n", server_ring->self_index, next_data);
    }

    NOTIFICATION new_notification = {.type = notification_type, .data = next_data};

    // Creating and configuring sockfd for the keepalive
    int sockfd = socket_create();
    server_ring_connect_with_next_server(server_ring, sockfd);

    // Went all the list around and couldn't connect to anyone, so I should be the primary anyway
    if (server_ring->next_index == server_ring->self_index)
    {
        logger_info("Couldn't find any other option connection, so I must be the only server\n");
        logger_info("I'm the new leader! 👑\n");
        server_ring->is_primary = 1;

        // Uses mutex because we don't know if it counts as a single memory access or two because of the pointer
        LOCK(server_ring->MUTEX_ELECTION);
        server_ring->in_election = 0;
        UNLOCK(server_ring->MUTEX_ELECTION);

        server_ring->primary_idx = server_ring->self_index;
        return;
    }

    // Need to ask who is the primary
    logger_info("Connected with next node in port %d\n", server_ring->server_ring_ports[server_ring->next_index]);
    logger_info("Will send the subsequent election message\n");

    int bytes_wrote = write(sockfd, (void *)&new_notification, sizeof(NOTIFICATION));
    if (bytes_wrote < 0)
    {
        logger_error("Error when trying to send next election message.\n");
        exit(ERROR_LOOKING_FOR_LEADER);
    }

    logger_info("Subsequent election message sent\n");

    close(sockfd);
}

void handle_connection_elected(NOTIFICATION *notification)
{

    // Just ignore in case we are electing ourselves, since we are the ones that started this message
    if (notification->data == server_ring->self_index)
    {
        logger_info("Received an ELECTED message, stating that I'm the leader, so everyone agrees now\n");
        return;
    }

    LOCK(server_ring->MUTEX_ELECTION);
    if (!server_ring->in_election)
    {
        logger_warn("Received elected message but it is not in an ellection. Will just drop the message...\n");
        UNLOCK(server_ring->MUTEX_ELECTION);
        return;
    }
    UNLOCK(server_ring->MUTEX_ELECTION);

    // Creating and configuring sockfd for the keepalive
    int sockfd = socket_create();

    logger_info("👑 The new leader is %d!\n", notification->data);

    // Configuring our primary port and election
    server_ring->primary_idx = notification->data;

    LOCK(server_ring->MUTEX_ELECTION);
    server_ring->in_election = 0;
    UNLOCK(server_ring->MUTEX_ELECTION);

    // Sending message to the next in the queue
    server_ring_connect_with_next_server(server_ring, sockfd);

    // Went all the list around and couldn't connect to anyone, so I should be the primary anyway
    if (server_ring->next_index == server_ring->self_index)
    {
        logger_error("Couldn't find any other option connection, so I should be the leader, but I'm not. Going to close for consistency...\n");
        exit(ERROR_ACCEPTING_CONNECTION);
    }

    // Need to send the message to the next one, saying who is the primary
    logger_info("Connected with next node in port %d\n", server_ring->server_ring_ports[server_ring->next_index]);
    logger_info("Will send the subsequent election message\n");

    int bytes_wrote = send(sockfd, (void *)notification, sizeof(NOTIFICATION), MSG_NOSIGNAL);
    if (bytes_wrote < 0)
    {
        logger_error("Error when trying to send elected message.\n");
        exit(ERROR_SENDING_ELECTED);
    }

    logger_info("Sent the ELECTED message stating that %d is the leader\n", notification->data);

    // Restart the keepalive process
    pthread_create(&server_ring->keepalive_tid, NULL, (void *(*)(void *)) & server_ring_keep_alive_primary, (void *)server_ring);

    close(sockfd);
}

void handle_connection_keepalive(int sockfd, NOTIFICATION *received_notification)
{
    if (received_notification->data != 1)
        send_initial_replication(sockfd);

    int bytes_read;
    NOTIFICATION notification = {.type = NOTIFICATION_TYPE__KEEPALIVE}, read_notification;

    while (1)
    {
        sleep(1);
        bytes_read = send(sockfd, (void *)&notification, sizeof(NOTIFICATION), MSG_NOSIGNAL);
        if (bytes_read < 0)
        {
            if (errno == EPIPE)
            {
                logger_info("[Socket %d] When sending keepalive to client. Must be dead. Stopping answering keep alives\n", sockfd);
                return;
            }

            logger_error("[Socket %d] When sending keepalive to client. Not an EPIPE. Will retry to send...\n", sockfd);
            continue;
        }

        bytes_read = read(sockfd, (void *)&read_notification, sizeof(NOTIFICATION));
        if (bytes_read < 0)
        {
            logger_info("[Socket %d] When receiving keepalive from the client. Must be dead. Stopping answering keep alives\n", sockfd);
            return;
        }
    }
}

void handle_connection_fe(int sockfd, NOTIFICATION *connection_notification)
{
    FE_SOCKFDS[connection_notification->data] = sockfd;
    HASH_NODE *hash_node;
    USER *user;

    // TODO: Keep hearing from this FE any incoming message
    while (1)
    {
        NOTIFICATION notification;
        int bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));
        if (bytes_read < 0)
        {
            logger_error("Couldn't read notification from socket %d\n", sockfd);
            return;
        }

        logger_info("Received NOTIFICATION from FE with id %d and type %d and message %s\n", notification.id, notification.type, notification.message);

        switch (notification.type)
        {
        case NOTIFICATION_TYPE__LOGIN:
            logger_info("[Socket %d] Received connection with LOGIN type\n", sockfd);
            handle_connection_login(sockfd, &notification);
            break;
        case NOTIFICATION_TYPE__LOGOUT:
            logger_info("[Socket %d] Received connection with LOGOUT type\n", sockfd);
            logout_user(notification.author);
            if (server_ring->is_primary)
            {
                send_replication(&notification);
            }
            break;
        case NOTIFICATION_TYPE__MESSAGE:
            logger_info("MESSAGE from author %s and other things %d %s\n", notification.author, notification.command, notification.receiver);
            hash_node = hash_find(user_hash_table, notification.author);
            hash_print(user_hash_table);
            logger_debug("Hash node author: %p\n", hash_node);
            user = (USER *)hash_node->value;
            process_message(&notification, user);
            break;
        default:
            logger_info("[Socket %d] Unhandable message with %d type. Ignoring...\n", sockfd, notification.type);
            break;
        }
    }

    close(sockfd);
}

void close_socket(void *void_socket)
{
    int socket = *((int *)void_socket);

    logger_debug("Closing socket %d...\n", socket);
    close(socket);
}

void cancel_thread(void *void_pthread)
{
    pthread_t thread_id = *((pthread_t *)void_pthread);

    logger_debug("Cancelling thread TID %li from thread TID %li...\n", (unsigned long int)thread_id, (unsigned long int)pthread_self());
    pthread_cancel(thread_id);
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

void handle_signals(void)
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);
    sigaction(SIGINT, &sigint_action, NULL); // Activating it twice works, so don't remove this ¯\_(ツ)_/¯

    struct sigaction sigint_ignore;
    sigint_action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sigint_ignore, NULL);
    sigaction(SIGPIPE, &sigint_ignore, NULL);

    // Create a different thread to handle CTRL + D
    pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(thread, NULL, (void *(*)(void *)) & handle_eof, (void *)NULL);
    logger_debug("Created new thread to handle EOF detection\n");

    chained_list_append_end(chained_list_threads, (void *)thread);
}

void *handle_eof(void *arg)
{
    const char BUFFER_SIZE = 5;
    char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    while (fgets(buffer, BUFFER_SIZE - 2, stdin))
    {
    }

    // Send a SIGINT to the parent
    kill(getpid(), SIGINT);

    return NULL;
}
