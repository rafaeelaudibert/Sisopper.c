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
#include "user.h"

int GLOBAL_ID = 0;

COMMAND identify_command(char *message)
{
    if (strncmp(message, "SEND ", NUMBER_OF_CHARS_IN_SEND) == 0)
    {
        logger_info("Its a SEND message\n");
        return SEND;
    }

    else if (strncmp(message, "FOLLOW ", NUMBER_OF_CHARS_IN_FOLLOW) == 0)
    {
        logger_info("Its a FOLLOW message\n");
        return FOLLOW;
    }

    return UNKNOWN;
}

char *remove_command_from_message(int command, char *message)
{
    if (command == FOLLOW)
        message = message + NUMBER_OF_CHARS_IN_FOLLOW;
    else if (command == SEND)
        message = message + NUMBER_OF_CHARS_IN_SEND;

    return message;
}

int main(int argc, char *argv[])
{
    int sockfd, bytes_read, command;
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

    int true = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1)
    {
        logger_error("On setting the socket configurations\n");
        exit(ERROR_CONFIGURATION_SOCKET);
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

    bytes_read = write(sockfd, (void *)user_handle, sizeof(MAX_USERNAME_LENGTH));
    if (bytes_read < 0)
    {
        logger_error("On sending user handle\n");
        exit(ERROR_STARTING_CONNECTION);
    }

    int can_login;
    bytes_read = read(sockfd, (void *)&can_login, sizeof(can_login));
    if (bytes_read < 0)
    {
        logger_error("On reading user login status\n");
        exit(ERROR_STARTING_CONNECTION);
    }
    if (!can_login)
    {
        logger_error("You cannot login. Max conections exceeded (%d)\n", MAX_SESSIONS);
        exit(ERROR_LOGIN);
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

        command = identify_command(buffer);
        if (command == UNKNOWN)
        {
            logger_info("Message unknown!\n");
            continue;
        }
        //TODO se command retornar -1 tem que printar mensagem para o user de mensagem invalida e não mandar o packet
        strcpy(buffer, remove_command_from_message(command, buffer));

        PACKET packet = {
            .command = command,
            .seqn = ++GLOBAL_ID,
            .timestamp = time(NULL),
            .length = strlen(buffer),
        };
        strncpy(packet.payload, buffer, MAX_MESSAGE_SIZE + 2);

        /* write in the socket */
        bytes_read = write(sockfd, (void *)&packet, sizeof(PACKET));
        if (bytes_read < 0)
            logger_error("On writing to socket\n");

        bzero(buffer, MAX_MESSAGE_SIZE + 2);

        /* read from the socket */
        bytes_read = read(sockfd, buffer, MAX_MESSAGE_SIZE);
        if (bytes_read < 0)
            logger_error("On reading from socket\n");

        logger_debug("ACK: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
