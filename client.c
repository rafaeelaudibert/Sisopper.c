#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>

#include "config.h"
#include "exit_errors.h"
#include "logger.h"
#include "notification.h"
#include "packet.h"
#include "user.h"

typedef int boolean;
#define FALSE 0
#define TRUE 1

static int received_sigint = FALSE;

long long MESSAGE_GLOBAL_ID = 0;

pthread_t read_thread_tid = -1;
int sockfd = -1;

void *handle_read(void *);
void handle_signals(void);
void sigint_handler(int);
void cleanup(int);

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

void get_input_message(char *buffer)
{
    /* Based on https://stackoverflow.com/a/38768287 */
    if (fgets(buffer, MAX_MESSAGE_SIZE, stdin))
    {
        char *p;
        if ((p = strchr(buffer, '\n')))
            *p = 0;
        else
        {
            (void) scanf("%*[^\n]");
            (void) scanf("%*c");
        }
    }
    buffer[strcspn(buffer, "\r\n")] = '\0'; // Replaces the first occurence of /[\n\r]/g with a \0
}

int main(int argc, char *argv[])
{
    int bytes_read, command;
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
        cleanup(ERROR_CONFIGURATION_SOCKET);
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
        cleanup(ERROR_STARTING_CONNECTION);
    }

    bytes_read = write(sockfd, (void *)user_handle, sizeof(char)*MAX_USERNAME_LENGTH);
    if (bytes_read < 0)
    {
        logger_error("On sending user handle\n");
        cleanup(ERROR_STARTING_CONNECTION);
    }

    int can_login;
    bytes_read = read(sockfd, (void *)&can_login, sizeof(can_login));
    if (bytes_read < 0)
    {
        logger_error("On reading user login status\n");
        cleanup(ERROR_STARTING_CONNECTION);
    }
    if (!can_login)
    {
        logger_error("You cannot login. Max conections exceeded (%d)\n", MAX_SESSIONS);
        cleanup(ERROR_LOGIN);
    }

    // Create a thread responsible for receiving notifications from the server
    pthread_create(&read_thread_tid, NULL, (void *(*)(void *)) & handle_read, (void *)&sockfd);
    logger_debug("Created new thread %ld to handle this connection\n", read_thread_tid);

    char buffer[MAX_MESSAGE_SIZE + 2];
    while (1)
    {
        // TODO: Configure so that if the person enters more than 128 characters,
        // it doesn't send as the next text message automatically
        printf("💬 Enter the message: ");
        bzero(buffer, MAX_MESSAGE_SIZE + 2);
        get_input_message(buffer);

        command = identify_command(buffer);
        if (command == UNKNOWN)
        {
            logger_info("Message type unknown! Please prepend the message with FOLLOW or SEND!\n");
            continue;
        }
        strcpy(buffer, remove_command_from_message(command, buffer));

        PACKET packet = {
            .command = command,
            .seqn = ++MESSAGE_GLOBAL_ID,
            .timestamp = time(NULL),
            .length = strlen(buffer),
        };
        strncpy(packet.payload, buffer, MAX_MESSAGE_SIZE + 2);

        /* write in the socket */
        bytes_read = write(sockfd, (void *)&packet, sizeof(PACKET));
        if (bytes_read < 0)
            logger_error("On writing to socket\n");
    }

    assert(0);
}

void *handle_read(void *void_sockfd)
{
    NOTIFICATION notification;
    int bytes_read, sockfd = *((int *)void_sockfd);

    while (1)
    {
        bzero((void *)&notification, sizeof(NOTIFICATION));

        /* read from the socket */
        bytes_read = read(sockfd, (void *)&notification, sizeof(NOTIFICATION));
        if (bytes_read < 0)
        {
            logger_error("[READ THREAD] On reading from socket\n");
        }
        else if (bytes_read == 0)
        {
            printf("\n"); // Add this empty line to skip the message prompt
            logger_info("[READ THREAD] Server closed connection\n");

            // Send a SIGINT to the parent
            kill(getpid(), SIGINT);

            // TODO: End the client interface
            return NULL;
        }
        else
        {
            logger_info("[READ THREAD] Received notification from server: %s\n", notification.message);
        }
    };

    return NULL;
}

void handle_signals(void)
{
    struct sigaction sigint_action;
    sigint_action.sa_handler = sigint_handler;
    sigaction(SIGINT, &sigint_action, NULL);
    sigaction(SIGINT, &sigint_action, NULL); // Activating it twice works, so don't remove this ¯\_(ツ)_/¯
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

void cleanup(int exit_code)
{
    pthread_cancel(read_thread_tid);
    close(sockfd);
    exit(exit_code);
}