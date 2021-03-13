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

#include "chained_list.h"
#include "exit_errors.h"
#include "logger.h"

#include "config.h"

#define FALSE 0
#define TRUE 1

#define BUFFER_SIZE 5

static int received_sigint = FALSE;

void *handle_connection(void *);
void close_socket(void *);
void join_thread(void *);
void sigint_handler(int);
void handle_signals(void);
void *handle_eof(void *);

CHAINED_LIST *chained_list_sockets_fd = NULL;
CHAINED_LIST *chained_list_threads = NULL;

int main(int argc, char *argv[])
{
    logger_debug("Initializing on debug mode!\n");

    handle_signals();

    int sockfd, exit_code = 0;
    socklen_t clilen = sizeof(struct sockaddr_in);
    struct sockaddr_in serv_addr, cli_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("On opening socket\n");
        exit(ERROR_OPEN_SOCKET);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        logger_error("On binding socket\n");
        exit_code = ERROR_BINDING_SOCKET;
        goto cleanup;
    }

    if (listen(sockfd, CONNECTIONS_TO_ACCEPT) < 0)
    {
        logger_error("On starting to listen");
        exit_code = ERROR_LISTEN;
        goto cleanup;
    }
    logger_info("Listening on port %d...\n", PORT);

    while (!received_sigint)
    {
        int *newsockfd = (int *)malloc(sizeof(int));
        *newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);

        // If received a sigint while it was blocked waiting for a new connection, we skip what we are doing below
        if (received_sigint)
            break;

        if (*newsockfd == -1)
        {
            logger_error("On accept\n");
            exit_code = ERROR_ACCEPT;
            goto cleanup;
        }

        logger_info("New connection from %s:%d\n", inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);

        pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
        pthread_create(thread, NULL, (void *(*)(void *)) & handle_connection, (void *)newsockfd);
        logger_debug("Created new thread to handle this connection\n");

        chained_list_append_end(chained_list_sockets_fd, (void *)newsockfd);
        chained_list_append_end(chained_list_threads, (void *)thread);
    }

cleanup:
    chained_list_iterate(chained_list_sockets_fd, &close_socket);
    chained_list_iterate(chained_list_threads, &join_thread);

    close(sockfd);

    return exit_code;
}

void *handle_connection(void *void_sockfd)
{
    char buffer[BUFFER_SIZE];
    int n, sockfd = *((int *)void_sockfd);

    logger_debug("[Socket %d] Hello from new thread to handle connection\n", sockfd);
    while (!received_sigint)
    {
        bzero(buffer, BUFFER_SIZE);

        /* read from the socket */
        n = read(sockfd, buffer, 256);
        if (n < 0 && !received_sigint)
        {
            logger_error("[Socket %d] On reading from socket\n", sockfd);
        }
        else if (n == 0)
        {
            logger_info("[Socket %d] Connection closed\n", sockfd);
            return NULL;
        }
        else
        {
            logger_info("[Socket %d] Here is the message: %s", sockfd, buffer);

            /* write the ack in the socket */
            n = write(sockfd, "I got your message", 18);
            if (n < 0)
                logger_error("[Socket %d] On writing to socket\n", sockfd);
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

void join_thread(void *void_pthread)
{
    pthread_t thread_id = *((pthread_t *)void_pthread);

    logger_debug("Joining thread PID %d...\n", thread_id);
    pthread_join(thread_id, NULL);
}

void sigint_handler(int _sigint)
{
    if (!received_sigint)
    {
        logger_warn("SIGINT received, closing descriptors and finishing...\n");
        received_sigint = TRUE;
    }
}

void handle_signals(void)
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);

    // Create a different thread to handle CTRL + D
    pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(thread, NULL, (void *(*)(void *)) & handle_eof, (void *)NULL);
    logger_debug("Created new thread to handle EOF detection\n");

    chained_list_append_end(chained_list_threads, (void *)thread);
}

void *handle_eof(void *arg)
{
    char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    while (fgets(buffer, BUFFER_SIZE - 2, stdin))
    {
    }

    // Send a signal to the parent
    kill(getpid(), SIGINT);

    return NULL;
}
