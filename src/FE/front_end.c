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

#define TRUE 1
#define FALSE 0

int serverRM_socket;
int serverRM_online = FALSE;

// Semaphores
sem_t online_semaphore;
sem_t users_map_semaphore;
sem_t sockets_map_semaphore;

void handle_signals(void);
void configure_connection(int *, sockaddr_in *)
void handle_server_connection(int *, pthread_t *)
void *listen_server_connection(void *)

int main(int argc, char *argv[])
{
    int i = 0;

    // Ports
    int portServerRM
    int portClients;

    // Sockets
    int serv_socket_fd = 0;
    int client_socket_fd = 0;

    // Sockets Address Config
    struct sockaddr_in serv_addr, client_addr;

    pthread_t tid[MAXBACKLOG];

    // Initializing Semaphores
    sem_init(&online_semaphore, 0, 1);
    sem_init(&users_map_semaphore, 0, 1);
    sem_init(&sockets_map_semaphore, 0, 1);

    handle_signals();


    if(argc == 3)
    {
        portServerRM = argv[1]
        portClientRM = argv[2]
    } else if(argc == 2)
    {
        portServerRM = argv[1]
    }

    // Server address config
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portServerRM);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    // Client address config
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(portClientRM);
    client_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    configure_connection(&serv_socket_fd, &serv_addr);
    configure_connection(&client_socket_fd, &client_addr);

    handle_server_connection(&serv_socket_fd, &tid[i++]);
    
    // TODO client connection

    return 0;
}

void handle_signals()
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);
    sigaction(SIGINT, &sigint_action, NULL); // Activating it twice works, so don't remove this ¯\_(ツ)_/¯
}

void configure_connection(int* sockfd, sockaddr_in* serv_addr)
{
    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("When opening socket\n");
        exit(ERROR_OPEN_SOCKET);
    }

    int true = 1;
    if (setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1)
    {
        logger_error("When setting the socket configurations\n");
        exit(ERROR_CONFIGURATION_SOCKET);
    }

    if (bind(*sockfd, (struct sockaddr *) serv_addr, sizeof(*serv_addr)) < 0){
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

void *listen_server_connection(void *void_sockfd)
{

}

void handle_server_connection(int* sockfd, pthread_t *tid)
{
    int *newsockfd = (int *)malloc(sizeof(int));
    if ((*newsockfd = accept(*sockfd, (struct sockaddr *)&cli_addr, &clilen) == -1)
        {
            logger_error("When accepting connection\n");
            cleanup(ERROR_ACCEPT);
            return -1;
        }

    pthread_create(tid, NULL, (void *(*)(void *)) &listen_server_connection, (void *)newsockfd);
    logger_debug("Created new thread %ld to handle this connection\n", *tid);
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