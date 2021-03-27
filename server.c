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

#include "config.h"

typedef int boolean;
#define FALSE 0
#define TRUE 1

static int received_sigint = FALSE;

void *handle_connection(void *);
void close_socket(void *);
void cancel_thread(void *);
void sigint_handler(int);
void handle_signals(void);
void *handle_eof(void *);
void cleanup(int);
USER *login_user(int);
void process_message(PACKET *, USER *);
void print_username(void *);
void follow_user(char *, char *);
void send_message(NOTIFICATION *, char *);

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;
int sockfd = 0;

HASH_TABLE user_hash_table = NULL;

// MUTEXES
pthread_mutex_t MUTEX_LOGIN = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_FOLLOW = PTHREAD_MUTEX_INITIALIZER;

#define LOCK(mutex) pthread_mutex_lock(&mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&mutex)

unsigned long long GLOBAL_NOTIFICATION_ID = 0;

int main(int argc, char *argv[])
{
    user_hash_table = hash_init();
    logger_debug("Initializing on debug mode!\n");

    handle_signals();

    socklen_t clilen = sizeof(struct sockaddr_in);
    struct sockaddr_in serv_addr, cli_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("On opening socket\n");
        exit(ERROR_OPEN_SOCKET);
    }

    int true = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1)
    {
        logger_error("On setting the socket configurations\n");
        exit(ERROR_CONFIGURATION_SOCKET);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(DEFAULT_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        logger_error("On binding socket\n");
        cleanup(ERROR_BINDING_SOCKET);
    }

    if (listen(sockfd, CONNECTIONS_TO_ACCEPT) < 0)
    {
        logger_error("On starting to listen");
        cleanup(ERROR_LISTEN);
    }
    logger_info("Listening on port %d...\n", DEFAULT_PORT);

    while (true)
    {
        int *newsockfd = (int *)malloc(sizeof(int));
        *newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);

        if (*newsockfd == -1)
        {
            logger_error("On accept\n");
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
        USER *user = (USER *)malloc(sizeof(USER));

        strcpy(user->username, username);
        user->sockets_fd[0] = sockfd;
        user->chained_list_followers = NULL;
        user->chained_list_notifications = NULL;
        user->sessions_number = 1;
        pthread_mutex_init(&user->mutex, NULL);

        for (int i = 1; i < MAX_SESSIONS; i++)
            user->sockets_fd[i] = -1;

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

void follow_user(char *user_to_follow_username, char *current_user_username)
{
    if (strcmp(user_to_follow_username, current_user_username) == 0)
    {
        logger_warn("User tried following itself, won't work!\n");

        NOTIFICATION notification = {
            .id = GLOBAL_NOTIFICATION_ID++,
            .timestamp = time(NULL),
            .message = "User tried following itself. This is not allowed.",
            .type = NOTIFICATION_TYPE__INFO};

        send_message(&notification, current_user_username);

        return;
    }

    // Only allow one follow to be processed at each given time
    LOCK(MUTEX_FOLLOW);

    HASH_NODE *followed_user_node = hash_find(user_hash_table, user_to_follow_username);
    if (followed_user_node == NULL)
    {
        char *error_message = (char *)calloc(150, sizeof(char));
        sprintf(error_message, "Could not follow the user %s. It doesn't exist", user_to_follow_username);

        NOTIFICATION notification = {
            .id = GLOBAL_NOTIFICATION_ID++,
            .timestamp = time(NULL),
            .type = NOTIFICATION_TYPE__INFO};
        strcpy(notification.message, error_message);

        send_message(&notification, current_user_username);

        logger_error(error_message);
    }
    else
    {
        USER *user = (USER *)followed_user_node->value;
        char *dup_current_user_username = strdup(current_user_username);

        // Lock because we are possibly going to play around with follow list
        LOCK(user->mutex);

        // In a real life, this probably should be a trie or any other tree-like structure
        // with O(logn) worst case, and not this O(n) solution
        char *list_current_user_username = (char *)chained_list_find(
            user->chained_list_followers,
            dup_current_user_username,
            (int (*)(void *, void *))strcmp);

        if (list_current_user_username == NULL)
        {
            user->chained_list_followers = chained_list_append_end(user->chained_list_followers, dup_current_user_username);
            logger_debug("New list of followers: ");
            chained_list_print(user->chained_list_followers, &print_username);

            char *info_message = (char *)calloc(150, sizeof(char));
            sprintf(info_message, "The user '%s' was followed!", user_to_follow_username);

            NOTIFICATION notification = {
                .id = GLOBAL_NOTIFICATION_ID++,
                .timestamp = time(NULL),
                .type = NOTIFICATION_TYPE__INFO};
            strcpy(notification.message, info_message);

            send_message(&notification, current_user_username);
        }
        else
        {
            char *error_message = (char *)calloc(150, sizeof(char));
            sprintf(error_message, "The user '%s' already follows '%s'", current_user_username, user_to_follow_username);

            NOTIFICATION notification = {
                .id = GLOBAL_NOTIFICATION_ID++,
                .timestamp = time(NULL),
                .type = NOTIFICATION_TYPE__INFO};
            strcpy(notification.message, error_message);

            send_message(&notification, current_user_username);

            logger_error(error_message);
        }

        UNLOCK(user->mutex);
    }

    UNLOCK(MUTEX_FOLLOW);
}

void process_message(PACKET *packet, USER *user)
{
    if (packet->command == FOLLOW)
    {
        logger_info("Following user: %s\n", packet->payload);
        follow_user(packet->payload, user->username);
    }
    else if (packet->command == SEND)
    {
        logger_info("Sending message: %s\n", packet->payload);
    }
}

// Sends a NOTIFICATION to a user
void send_message(NOTIFICATION *notification, char *username)
{

    HASH_NODE *node = hash_find(user_hash_table, username);
    if (!node)
    {
        logger_error("When sending message to non existent username %s\n", username);
    }

    // Gets the user and lock its mutex
    USER *user = (USER *)node->value;
    LOCK(user->mutex);

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        int socket_fd = user->sockets_fd[i];
        if (socket_fd != -1)
        {
            if (write(socket_fd, notification, sizeof(NOTIFICATION)) < 0)
            {
                logger_error("When sending notification %d to %s through socket %d\n", notification->id, user->username, socket_fd);
            }
        }
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
        logger_error("[Socket %d] On sending login ACK/NACK (%d)\n", sockfd, can_login);
        return NULL;
    }
    if (!can_login)
    {
        logger_error("User couldn't login! Max connections (%d) reached\n", MAX_SESSIONS);
        return NULL;
    }

    while (1)
    {
        bzero((void *)&packet, sizeof(PACKET));

        /* read from the socket */
        bytes_read = read(sockfd, (void *)&packet, sizeof(PACKET));
        if (bytes_read < 0)
        {
            logger_error("[Socket %d] On reading from socket\n", sockfd);
        }
        else if (bytes_read == 0)
        {
            logger_info("[Socket %d] Client closed connection\n", sockfd);

            /* TODO: Seção crítica */
            current_user->sessions_number--;
            /* TODO: END seção crítica */

            return NULL;
        }
        else
        {
            logger_info("[Socket %d] Here is the message: %s\n", sockfd, packet.payload);
            process_message(&packet, current_user);
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
