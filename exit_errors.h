#ifndef EXIT_ERRORS_H
#define EXIT_ERRORS_H

enum EXIT_ERRORS
{
    // Common
    ERROR_OPEN_SOCKET = 1,
    ERROR_CONFIGURATION_SOCKET,

    // Server
    ERROR_ACCEPTING_CONNECTION,
    ERROR_BINDING_SOCKET,
    ERROR_LISTEN,
    ERROR_ACCEPT,

    // Client
    NOT_ENOUGH_ARGUMENTS_ERROR,
    INCORRECT_HANDLE_ERROR,
    INCORRECT_HOST_ERROR,
    ERROR_STARTING_CONNECTION,
    ERROR_LOGIN,
};

#endif // EXIT_ERRORS_H