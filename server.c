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

static int should_listen_for_connections = TRUE;

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
    handle_signals();

    int sockfd;
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
        exit(ERROR_BINDING_SOCKET);
    }

    listen(sockfd, CONNECTIONS_TO_ACCEPT);
    logger_info("Listening on port %d...\n", PORT);

    while (should_listen_for_connections)
    {
        int *newsockfd = (int *)malloc(sizeof(int));
        if ((*newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) == -1)
            logger_error("On accept\n");
        logger_info("New connection from %s:%d | %d\n", inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port, cli_addr.sin_family);

        pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
        pthread_create(thread, NULL, (void *(*)(void *)) & handle_connection, (void *)newsockfd);
        logger_info("Created new thread to handle this connection\n");

        chained_list_append_end(chained_list_sockets_fd, (void *)newsockfd);
        chained_list_append_end(chained_list_threads, (void *)thread);
    }

    chained_list_iterate(chained_list_sockets_fd, &close_socket);
    chained_list_iterate(chained_list_threads, &join_thread);
    close(sockfd);

    return 0;
}

void *handle_connection(void *void_sockfd)
{
    char buffer[BUFFER_SIZE];
    int n, sockfd = *((int *)void_sockfd);

    logger_info("[Socket %d] Hello from new thread to handle connection\n", sockfd);
    while (should_listen_for_connections)
    {
        bzero(buffer, BUFFER_SIZE);

        /* read from the socket */
        n = read(sockfd, buffer, 256);
        if (n < 0 && should_listen_for_connections)
        {
            logger_error("[Socket %d] On reading from socket\n", sockfd);
        }
        else if (n == 0)
        {
            logger_warn("[Socket %d] Connection closed\n", sockfd);
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
    close(socket);
}

void join_thread(void *void_pthread)
{
    pthread_t thread_id = *((pthread_t *)void_pthread);
    pthread_join(thread_id, NULL);
}

void sigint_handler(int _sigint)
{
    logger_warn("SIGINT received, closing descriptors and finishing...\n");
    should_listen_for_connections = FALSE;
}

void handle_signals(void)
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);

    // Create a different thread to handle CTRL + D
    pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(thread, NULL, (void *(*)(void *)) & handle_eof, (void *)NULL);
    logger_info("Created new thread to handle EOF detection\n");

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
