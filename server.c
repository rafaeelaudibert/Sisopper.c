#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>

#include "logger.h"
#include "exit_errors.h"

#define PORT 4000
#define CONNECTIONS_TO_ACCEPT 5

#define FALSE 0
#define TRUE 1

static int keep_running = TRUE;

void sigint_handler(int _sigint)
{
    logger_warn("SIGINT received, closing descriptors and finishing...");
    keep_running = FALSE;
}

void handle_signals(void)
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);
}

int main(int argc, char *argv[])
{
    handle_signals();

    int sockfd, newsockfd, n;
    socklen_t clilen = sizeof(struct sockaddr_in);
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("Opening socket");
        exit(ERROR_OPEN_SOCKET);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(serv_addr.sin_zero), 8);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        logger_error("Binding socket");
        exit(ERROR_BINDING_SOCKET);
    }

    listen(sockfd, CONNECTIONS_TO_ACCEPT);

    if ((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) == -1)
    {
        logger_error("On accept");
        exit(ERROR_ACCEPTING_CONNECTION);
    }

    while (keep_running)
    {
        bzero(buffer, 256);

        /* read from the socket */
        n = read(newsockfd, buffer, 256);
        if (n < 0 && keep_running)
        {
            logger_error("reading from socket");
        }
        else if (n == 0)
        {
            logger_info("Not read anything, connection should have been closed then");
            keep_running = FALSE;
        }
        else
        {
            logger_info("Here is the message: %s", buffer);

            /* write the ack in the socket */
            n = write(newsockfd, "I got your message", 18);
            if (n < 0)
                logger_error("writing to socket");
        }
    }
    close(newsockfd);

    close(sockfd);
    return 0;
}