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
USER *login_user(int sockfd);
void process_message(PACKET packet, USER user);
void print_username(void* void_parameter);
void follow_user(char* user_to_follow_username, char* current_user_username);

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;
int sockfd = 0;

int main(int argc, char *argv[])
{
    hashInit();
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

    int bytes_read = read(sockfd, (void *)username, sizeof(char)*MAX_USERNAME_LENGTH);
    if (bytes_read < 0)
    {
        logger_error("Couldn't read username for socket %d\n", sockfd);
        return NULL;
    }

    //TODO BEGIN seção critica
    HASH_USER *hash_user = hashFind(username);
    if (!hash_user)
    {
        logger_info("New user logged: %s\n", username);
        USER *user = (USER *)malloc(sizeof(USER));

        strcpy(user->username, username);
        user->sockets_fd[0] = sockfd;
        user->sockets_fd[1] = -1;
        user->chained_list_followers = NULL;
        user->chained_list_notifications = NULL;
        user->sessions_number = 1;
        hash_user = hashInsert(username, *user);

        return &hash_user->user;
    }
    else if (hash_user->user.sessions_number < MAX_SESSIONS)
    {
        int free_socket_spot = get_free_socket_spot(hash_user->user.sockets_fd);
        logger_info("User in another session: %s\n", hash_user->username);
        logger_info("sessions: %d\n", hash_user->user.sessions_number);
        logger_info("Saving socket in position: %d\n", free_socket_spot);
        hash_user->user.sockets_fd[free_socket_spot] = sockfd;
        hash_user->user.sessions_number++;

        return &hash_user->user;
    }
    // TODO: END SEÇÃO CRÍTICA

    return NULL;
}


void print_username(void *void_parameter)
{
    logger_info("HERE111\n");
    char* parameter = ((char*) void_parameter);
    logger_info("HERE333\n");
    printf("%s", parameter);
    logger_info("HERE444\n");
}

void follow_user(char* user_to_follow_username, char* current_user_username)
{
    HASH_USER *hash_user = hashFind(user_to_follow_username);
    if(hash_user)
    {
        hash_user->user.chained_list_followers = chained_list_append_end(hash_user->user.chained_list_followers, current_user_username);
        logger_info("HERE\n");
        chained_list_print(hash_user->user.chained_list_followers, &print_username);
        logger_info("HERE2\n");
    }
    else
        logger_error("Could not follow the user %s\n", user_to_follow_username);
}

void process_message(PACKET packet, USER user)
{
    if (packet.command == FOLLOW)
    {
        logger_info("Following user: %s\n", packet.payload);
        follow_user(packet.payload, user.username);

        // TODO: SEÇÃO CRÍTICA DE FOLLOW

        // TODO: END SEÇÃO CRÍTICA DE FOLLOW
    }
    else if (packet.command == SEND)
    {
        logger_info("Sending message: %s\n", packet.payload);
        //TODO send the message to the followers
    }
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
            process_message(packet, *current_user);
            logger_info("[Socket %d] Here is the message: %s\n", sockfd, packet.payload);

            /* write the ack in the socket */
            bytes_read = write(sockfd, "I got your message", 18);
            if (bytes_read < 0)
                logger_error("[Socket %d] On writing ACK to socket\n", sockfd);
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
