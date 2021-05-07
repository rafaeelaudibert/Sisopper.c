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

#include "chained_list.h"
#include "exit_errors.h"
#include "logger.h"
#include "packet.h"
#include "user.h"
#include "hash.h"
#include "notification.h"
#include "savefile.h"

#include "config.h"

typedef int boolean;
#define FALSE 0
#define TRUE 1

static int received_sigint = FALSE;
static int server_port = DEFAULT_PORT;

void *handle_connection(void *);
void close_socket(void *);
void cancel_thread(void *);
void sigint_handler(int);
void handle_signals(void);
void *handle_eof(void *);
void cleanup(int);
USER *login_user(int);
void process_message(void *);
void receive_message(PACKET *, USER *);
void follow_user(PACKET *, USER *);
void print_username(void *);
void send_message(NOTIFICATION *, char *);

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;
int sockfd = 0;

HASH_TABLE user_hash_table = NULL;

// MUTEXES
pthread_mutex_t MUTEX_LOGIN = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_FOLLOW = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_PENDING_NOTIFICATIONS = PTHREAD_MUTEX_INITIALIZER;

#define LOCK(mutex) pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)

unsigned long long GLOBAL_NOTIFICATION_ID = 0;

typedef struct
{
    PACKET *packet;
    USER *user;
} MESSAGE_TO_PROCESS;

int main(int argc, char *argv[])
{
    user_hash_table = read_savefile();
    logger_debug("Initializing on debug mode!\n");

    handle_signals();

    socklen_t clilen = sizeof(struct sockaddr_in);
    struct sockaddr_in serv_addr, cli_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("When opening socket\n");
        exit(ERROR_OPEN_SOCKET);
    }

    int true = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1)
    {
        logger_error("When setting the socket configurations\n");
        exit(ERROR_CONFIGURATION_SOCKET);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    while (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        server_port++;
        serv_addr.sin_port = htons(server_port);
    }

    if (listen(sockfd, CONNECTIONS_TO_ACCEPT) < 0)
    {
        logger_error("When starting to listen");
        cleanup(ERROR_LISTEN);
    }
    logger_info("Listening on port %d...\n", server_port);

    // This loop is responsible for keep accepting new connections from clients
    while (true)
    {
        int *newsockfd = (int *)malloc(sizeof(int));
        *newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);

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
        logger_info("sessions: %d\n", user->sessions_number);
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

void print_username(void *void_parameter)
{
    char *parameter = ((char *)void_parameter);
    printf("%s", parameter);
}

void follow_user(PACKET *packet, USER *current_user)
{
    char *user_to_follow_username = packet->payload;

    if (strcmp(user_to_follow_username, current_user->username) == 0)
    {
        logger_warn("User tried following itself, won't work!\n");

        NOTIFICATION notification = {
            .id = GLOBAL_NOTIFICATION_ID++,
            .timestamp = time(NULL),
            .message = "User tried following itself. This is not allowed.",
            .type = NOTIFICATION_TYPE__INFO};

        send_message(&notification, current_user->username);

        return;
    }

    // Only allow one follow to be processed at each given time
    LOCK(MUTEX_FOLLOW);

    HASH_NODE *followed_user_node = hash_find(user_hash_table, user_to_follow_username);
    if (followed_user_node == NULL)
    {
        char *error_message = (char *)calloc(220, sizeof(char));
        sprintf(error_message, "Could not follow the user %s. It doesn't exist", user_to_follow_username);

        NOTIFICATION notification = {
            .id = GLOBAL_NOTIFICATION_ID++,
            .timestamp = time(NULL),
            .type = NOTIFICATION_TYPE__INFO};
        strcpy(notification.message, error_message);

        send_message(&notification, current_user->username);

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
            user->followers = chained_list_append_end(user->followers, dup_current_user_username);
            logger_debug("New list of followers: ");
            chained_list_print(user->followers, &print_username);

            char *info_message = (char *)calloc(220, sizeof(char));
            sprintf(info_message, "The user '%s' was followed!", user_to_follow_username);

            NOTIFICATION notification = {
                .id = GLOBAL_NOTIFICATION_ID++,
                .timestamp = time(NULL),
                .type = NOTIFICATION_TYPE__INFO};
            strcpy(notification.message, info_message);

            send_message(&notification, current_user->username);
        }
        else
        {
            char *error_message = (char *)calloc(220, sizeof(char));
            sprintf(error_message, "The user '%s' already follows '%s'", current_user->username, user_to_follow_username);

            NOTIFICATION notification = {
                .id = GLOBAL_NOTIFICATION_ID++,
                .timestamp = time(NULL),
                .type = NOTIFICATION_TYPE__INFO};
            strcpy(notification.message, error_message);

            send_message(&notification, current_user->username);

            logger_error(error_message);
        }

        UNLOCK(user->mutex);
    }

    UNLOCK(MUTEX_FOLLOW);
}

void process_message(void *void_message_to_process)
{
    MESSAGE_TO_PROCESS *message_to_process = (MESSAGE_TO_PROCESS *)void_message_to_process;
    if (message_to_process->packet->command == FOLLOW)
    {
        logger_info("Following user: %s\n", message_to_process->packet->payload);
        follow_user(message_to_process->packet, message_to_process->user);
    }
    else if (message_to_process->packet->command == SEND)
    {
        logger_info("Received message: %s\n", message_to_process->packet->payload);
        receive_message(message_to_process->packet, message_to_process->user);
    }
}

void receive_message(PACKET *packet, USER *current_user)
{
    NOTIFICATION *notification = (NOTIFICATION *)calloc(1, sizeof(NOTIFICATION));
    strcpy(notification->author, current_user->username);
    strcpy(notification->message, packet->payload);
    notification->id = GLOBAL_NOTIFICATION_ID++;
    notification->timestamp = packet->timestamp;
    notification->type = NOTIFICATION_TYPE__MESSAGE;

    LOCK(MUTEX_FOLLOW);
    CHAINED_LIST *follower = current_user->followers;
    send_message(notification, current_user->username);
    while (follower)
    {
        send_message(notification, (char *)follower->val);
        follower = follower->next;
    }
    UNLOCK(MUTEX_FOLLOW);

    // Lock user to update list of notification
    LOCK(current_user->mutex);
    current_user->notifications = chained_list_append_end(current_user->notifications, (void *)notification);
    UNLOCK(current_user->mutex);
}

// Sends a NOTIFICATION to a user
void send_message(NOTIFICATION *notification, char *username)
{

    HASH_NODE *node = hash_find(user_hash_table, username);
    if (!node)
    {
        logger_error("When sending message to non existent username %s\n", username);
        return;
    }

    // Gets the user and lock its mutex
    USER *user = (USER *)node->value;
    LOCK(user->mutex);

    if (user->sessions_number > 0)
    {
        for (int i = 0; i < MAX_SESSIONS; i++)
        {
            int socket_fd = user->sockets_fd[i];
            if (socket_fd != -1)
            {
                if (write(socket_fd, notification, sizeof(NOTIFICATION)) < 0)
                    logger_error("When sending notification %d to %s through socket %d\n", notification->id, user->username, socket_fd);
                else
                    logger_info("Sent notification %d with message '%s' to %s on socket %d\n", notification->id, notification->message, username, socket_fd);
            }
        }
    }
    else
    {
        logger_info("Added notification %ld with message '%s' to be sent later to %s\n", notification->id, notification->message, username);
        // Add to the notifications which must be sent to this user later on
        LOCK(MUTEX_PENDING_NOTIFICATIONS);
        user->pending_notifications = chained_list_append_end(user->pending_notifications, (void *)notification);
        UNLOCK(MUTEX_PENDING_NOTIFICATIONS);
    }

    UNLOCK(user->mutex);
}

void *handle_connection(void *void_sockfd)
{
    PACKET packet;
    int bytes_read, sockfd = *((int *)void_sockfd);

    USER *current_user = login_user(sockfd);
    boolean can_login = current_user != NULL;

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

    // Do not allow to add any new notification while we haven't sent the others
    LOCK(MUTEX_PENDING_NOTIFICATIONS);
    CHAINED_LIST *pending_notification = current_user->pending_notifications;
    while (pending_notification)
    {
        NOTIFICATION *notification = (NOTIFICATION *)pending_notification->val;

        logger_info("[Socket %d] Sending pending notification from %d to %d\n", sockfd, notification->author, current_user->username);
        send_message(notification, current_user->username);

        pending_notification = pending_notification->next;
    }

    // We have sent them all, so we can clean it
    current_user->pending_notifications = NULL;
    UNLOCK(MUTEX_PENDING_NOTIFICATIONS);

    while (1)
    {
        bzero((void *)&packet, sizeof(PACKET));

        /* read from the socket */
        bytes_read = read(sockfd, (void *)&packet, sizeof(PACKET));
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
                    logger_info("[Socket %d] Freed %d socket position\n", i);
                    current_user->sockets_fd[i] = -1;
                    break;
                }
            UNLOCK(current_user->mutex);

            return NULL;
        }
        else
        {
            pthread_t tid;

            // Create copy to pass to the other thread and not be overriden
            PACKET *packet_copy = (PACKET *)calloc(1, sizeof(PACKET));
            memcpy(packet_copy, &packet, sizeof(PACKET));
            logger_info("[Socket %d] Here is the message: %s\n", sockfd, packet_copy->payload);

            MESSAGE_TO_PROCESS *message_to_process = (MESSAGE_TO_PROCESS *)calloc(1, sizeof(MESSAGE_TO_PROCESS));
            message_to_process->packet = packet_copy;
            message_to_process->user = current_user;
            pthread_create(&tid, NULL, (void *(*)(void *)) & process_message, (void *)message_to_process);
            logger_info("[Socket %d] Proccessing message %d on brand new thread %ld\n", sockfd, packet_copy->seqn, tid);
        }
    };

    return NULL;
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
