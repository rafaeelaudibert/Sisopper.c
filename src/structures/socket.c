#include "socket.h"

#include "logger.h"
#include "exit_errors.h"

#include <stdlib.h>
#include <sys/socket.h>

int socket_create(void)
{
    // Creating and configuring sockfd for the keepalive
    int sockfd, true = 1;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("When opening socket\n");
        exit(ERROR_OPEN_SOCKET);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1)
    {
        logger_error("When setting the socket configurations\n");
        exit(ERROR_CONFIGURATION_SOCKET);
    }

    return sockfd;
}