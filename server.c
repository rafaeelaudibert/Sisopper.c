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

    close(sockfd);

    exit(exit_code);
}

void login_user(int sockfd)
{
    //TODO seção critica
    char username[MAX_USERNAME_LENGTH];
    USER *user = (USER *)malloc(sizeof(USER));
    ;
    HASH_USER *hash_user;

    read(sockfd, (void *)username, sizeof(MAX_USERNAME_LENGTH));
    hash_user = hashFind(username);
    if (hash_user == 0)
    {
        logger_info("New user logged: %s\n", username);
        user->sockets_fd[0] = sockfd;
        user->chained_list_followers = NULL;
        user->chained_list_notifications = NULL;
        user->sessions_number = 1;
        hashInsert(username, *user);
    }
    else if (hash_user->user.sessions_number <= 2)
    {
        logger_info("User in another session: %s\n", hash_user->username);
        user->sockets_fd[1] = sockfd;
        user->sessions_number++;
    }
    else
    {
        //TODO: Precisa barrar o usuário de conectar porque esta em mais de duas sessões
    }

    hashPrint();
}

void process_message(PACKET packet)
{
    if (packet.command == FOLLOW)
    {
        logger_info("Following user: %s\n", packet.payload);
        //TODO follow the user
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

    login_user(sockfd);

    while (!received_sigint)
    {
        bzero((void *)&packet, sizeof(PACKET));

        /* read from the socket */
        bytes_read = read(sockfd, (void *)&packet, sizeof(PACKET));
        if (bytes_read < 0 && !received_sigint)
        {
            logger_error("[Socket %d] On reading from socket\n", sockfd);
        }
        else if (bytes_read == 0)
        {
            logger_info("[Socket %d] Connection closed\n", sockfd);
            return NULL;
        }
        else
        {
            process_message(packet);
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
    const char BUFFER_SIZE = 100;
    char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    while (fgets(buffer, BUFFER_SIZE - 2, stdin))
    {
    }

    // Send a signal to the parent
    kill(getpid(), SIGINT);

    return NULL;
}
