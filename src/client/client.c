#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
#include "ui.h"

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
        return SEND;
    if (strncmp(message, "FOLLOW ", NUMBER_OF_CHARS_IN_FOLLOW) == 0)
        return FOLLOW;

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
    int bytes_read, command;
    struct sockaddr_in serv_addr;

    if (argc < 2)
    {
        logger_info("Usage: %s <profile> <server address=%s> <port=%d>\n", argv[0], DEFAULT_HOST, DEFAULT_PORT);
        exit(NOT_ENOUGH_ARGUMENTS_ERROR);
    }

    srand((unsigned)time(0));

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

    UI_start(user_handle);

    struct hostent *server = argc >= 3 ? gethostbyname(argv[2]) : gethostbyname(DEFAULT_HOST);
    if (server == NULL)
    {
        char *error_message = "No such host";
        UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
        ui_error_message->timestamp = time(NULL);
        ui_error_message->message = strdup(error_message);
        ui_error_message->type = UI_MESSAGE_TYPE__INFO;
        UI_add_new_message(ui_error_message);

        UI_end();
        exit(INCORRECT_HOST_ERROR);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        char *error_message = "Error when opening socket";
        UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
        ui_error_message->timestamp = time(NULL);
        ui_error_message->message = strdup(error_message);
        ui_error_message->type = UI_MESSAGE_TYPE__INFO;
        UI_add_new_message(ui_error_message);

        UI_end();
        exit(ERROR_OPEN_SOCKET);
    }

    int this_true = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &this_true, sizeof(int)) == -1)
    {
        char *error_message = "Error when setting the socket configurations";
        UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
        ui_error_message->timestamp = time(NULL);
        ui_error_message->message = strdup(error_message);
        ui_error_message->type = UI_MESSAGE_TYPE__INFO;
        UI_add_new_message(ui_error_message);

        cleanup(ERROR_CONFIGURATION_SOCKET);
    }

    int port = argc >= 4 ? atoi(argv[3]) : DEFAULT_PORT;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(serv_addr.sin_zero), 8);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        char *error_message = "Error when connecting to server";
        UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
        ui_error_message->timestamp = time(NULL);
        ui_error_message->message = strdup(error_message);
        ui_error_message->type = UI_MESSAGE_TYPE__INFO;
        UI_add_new_message(ui_error_message);

        cleanup(ERROR_STARTING_CONNECTION);
    }

    bytes_read = write(sockfd, (void *)user_handle, sizeof(char) * MAX_USERNAME_LENGTH);
    if (bytes_read < 0)
    {
        char *error_message = "Error when sending user handle";
        UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
        ui_error_message->timestamp = time(NULL);
        ui_error_message->message = strdup(error_message);
        ui_error_message->type = UI_MESSAGE_TYPE__INFO;
        UI_add_new_message(ui_error_message);

        cleanup(ERROR_STARTING_CONNECTION);
    }

    int can_login;
    bytes_read = read(sockfd, (void *)&can_login, sizeof(can_login));
    if (bytes_read < 0)
    {
        char *error_message = "Error when reading user login status";
        UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
        ui_error_message->timestamp = time(NULL);
        ui_error_message->message = strdup(error_message);
        ui_error_message->type = UI_MESSAGE_TYPE__INFO;
        UI_add_new_message(ui_error_message);

        cleanup(ERROR_STARTING_CONNECTION);
    }
    if (!can_login)
    {
        char *error_message = (char *)calloc(60, sizeof(char));
        sprintf(error_message, "You cannot login. Max conections exceeded (%d)\n", MAX_SESSIONS);

        UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
        ui_error_message->timestamp = time(NULL);
        ui_error_message->message = strdup(error_message);
        ui_error_message->type = UI_MESSAGE_TYPE__INFO;
        UI_add_new_message(ui_error_message);

        cleanup(ERROR_LOGIN);
    }

    // Create a thread responsible for receiving notifications from the server
    pthread_create(&read_thread_tid, NULL, (void *(*)(void *)) & handle_read, (void *)&sockfd);

    // Message should be at most MAX_MESSAGE_SIZE characters, without considering commands.
    while (1)
    {
        char *buffer = UI_get_text(MAX_MESSAGE_SIZE + NUMBER_OF_CHARS_IN_SEND);

        command = identify_command(buffer);
        if (command == UNKNOWN)
        {
            char *info_message = "This message type is unknown! Please prepend the message with FOLLOW or SEND!";
            UI_MESSAGE *ui_info_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
            ui_info_message->timestamp = time(NULL);
            ui_info_message->message = strdup(info_message);
            ui_info_message->type = UI_MESSAGE_TYPE__INFO;
            UI_add_new_message(ui_info_message);
            continue;
        }

        strcpy(buffer, remove_command_from_message(command, buffer));

        PACKET packet = {
            .command = command,
            .seqn = ++MESSAGE_GLOBAL_ID,
            .timestamp = time(NULL),
            .length = strlen(buffer),
        };
        strcpy(packet.payload, buffer);

        /* write in the socket */
        bytes_read = write(sockfd, (void *)&packet, sizeof(PACKET));
        if (bytes_read < 0)
        {
            char *error_message = "Error when writing to server";
            UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
            ui_error_message->timestamp = time(NULL);
            ui_error_message->message = strdup(error_message);
            ui_error_message->type = UI_MESSAGE_TYPE__INFO;
            UI_add_new_message(ui_error_message);
        }
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
            char *error_message = "Error when receiving message from the server";
            UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
            ui_error_message->timestamp = time(NULL);
            ui_error_message->message = strdup(error_message);
            ui_error_message->type = UI_MESSAGE_TYPE__INFO;
            UI_add_new_message(ui_error_message);
        }
        else if (bytes_read == 0)
        {
            char *error_message = "The server closed the connection";
            UI_MESSAGE *ui_error_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
            ui_error_message->timestamp = time(NULL);
            ui_error_message->message = strdup(error_message);
            ui_error_message->type = UI_MESSAGE_TYPE__INFO;
            UI_add_new_message(ui_error_message);

            // Send a SIGINT to the parent
            kill(getpid(), SIGINT);

            return NULL;
        }
        else
        {
            UI_MESSAGE *ui_message = (UI_MESSAGE *)calloc(1, sizeof(UI_MESSAGE));
            ui_message->timestamp = notification.timestamp;
            ui_message->message = strdup(notification.message);

            // Specific for each notification type
            if (notification.type == NOTIFICATION_TYPE__MESSAGE)
            {
                ui_message->author = strdup(notification.author);
                ui_message->type = UI_MESSAGE_TYPE__MESSAGE;
            }
            else if (notification.type == NOTIFICATION_TYPE__INFO)
            {
                ui_message->type = UI_MESSAGE_TYPE__INFO;
            }

            UI_add_new_message(ui_message);
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
    if (read_thread_tid != -1)
        pthread_cancel(read_thread_tid);

    if (sockfd != -1)
        close(sockfd);

    UI_end();

    exit(exit_code);
}
