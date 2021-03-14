#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "config.h"
#include "exit_errors.h"
#include "logger.h"
#include "packet.h"

int GLOBAL_ID = 0;

int main(int argc, char *argv[])
{
    int sockfd, n;
    struct sockaddr_in serv_addr;

    if (argc < 2)
    {
        logger_info("Usage: %s <profile> <server address=%s> <port=%d>\n", argv[0], DEFAULT_HOST, DEFAULT_PORT);
        exit(NOT_ENOUGH_ARGUMENTS_ERROR);
    }

    char *user_handle = argv[1];
    int len_handle = strlen(user_handle);
    if (len_handle < HANDLE_MIN_SIZE || len_handle > HANDLE_MAX_SIZE || argv[1][0] != HANDLE_FIRST_CHARACTER)
    {
        logger_error(
            "<profile> must start with a '%c' character, and contain at least %d and at most %d characters. You passed %s.\n",
            HANDLE_FIRST_CHARACTER,
            HANDLE_MIN_SIZE,
            HANDLE_MAX_SIZE,
            user_handle);
        exit(INCORRECT_HANDLE_ERROR);
    }
    logger_debug("Will connect with handle %s\n", user_handle);

    struct hostent *server = argc >= 3 ? gethostbyname(argv[2]) : gethostbyname(DEFAULT_HOST);
    if (server == NULL)
    {
        logger_error("No such host\n");
        exit(INCORRECT_HOST_ERROR);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logger_error("On opening socket\n");
        exit(ERROR_OPEN_SOCKET);
    }

    int port = argc >= 4 ? atoi(argv[3]) : DEFAULT_PORT;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(serv_addr.sin_zero), 8);
    logger_debug("Will connect to %s:%d\n", server->h_name, port);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        logger_error("On connecting\n");
        close(sockfd);
        exit(ERROR_STARTING_CONNECTION);
    }

    char buffer[MAX_MESSAGE_SIZE + 2];
    while (1)
    {
        // TODO: Configure so that if the person enters more than 128 characters,
        // it doesn't send as the next text message automatically
        printf("💬 Enter the message: ");
        bzero(buffer, MAX_MESSAGE_SIZE + 2);
        fgets(buffer, MAX_MESSAGE_SIZE, stdin);
        buffer[strcspn(buffer, "\r\n")] = '\0'; // Replaces the first occurence of /[\n\r]/g with a \0

        /* write in the socket */
        n = write(sockfd, buffer, strlen(buffer));
        if (n < 0)
            logger_error("On writing to socket\n");

        bzero(buffer, MAX_MESSAGE_SIZE + 2);

        /* read from the socket */
        n = read(sockfd, buffer, MAX_MESSAGE_SIZE);
        if (n < 0)
            logger_error("On reading from socket\n");

        logger_debug("ACK: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}