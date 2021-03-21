#ifndef CONFIG_H
#define CONFIG_H

// Common
#define DEFAULT_PORT 4000
#define DEFAULT_HOST "localhost"
#define MAX_SESSIONS 2
#define MAX_USERNAME_LENGTH 20

// Server
#define CONNECTIONS_TO_ACCEPT 15

// Client
#define HANDLE_MIN_SIZE 4
#define HANDLE_MAX_SIZE 20
#define MAX_MESSAGE_SIZE 128
#define HANDLE_FIRST_CHARACTER '@'

#endif // CONFIG_H