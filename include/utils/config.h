#ifndef CONFIG_H
#define CONFIG_H

// Common
#define MAX_SESSIONS 2
#define MAX_USERNAME_LENGTH 20

// Server
#define CONNECTIONS_TO_ACCEPT 15
#define SAVEFILE_FILE_PATH ".savefile"

// Client
#define HANDLE_MIN_SIZE 4
#define HANDLE_MAX_SIZE 20
#define MAX_MESSAGE_SIZE 128
#define HANDLE_FIRST_CHARACTER '@'
#define NUMBER_OF_CHARS_IN_SEND 5
#define NUMBER_OF_CHARS_IN_FOLLOW 7

int FE_PORTS[] = {12001, 12002, 12003, 12004, 12005};
char *FE_HOSTS[] = {"127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1"}
#define NUMBER_OF_FES (sizeof(FE_PORTS) / sizeof(FE_PORTS[0]))

#endif // CONFIG_H