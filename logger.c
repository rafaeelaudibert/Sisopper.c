#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include "logger.h"

char *get_current_timestamp()
{
    size_t buffer_size = 900 * sizeof(char);

    time_t now;
    struct tm ts;
    char *buf = (char *)malloc(buffer_size);

    // Get current time
    time(&now);
    ts = *localtime(&now);
    strftime(buf, buffer_size, "%d/%m/%Y %H:%M:%S", &ts);

    return buf;
}

int logger_error(char *fmt, ...)
{
    int ret;
    va_list myargs;

    char *current_timestmap = get_current_timestamp();

    /* Initialise the va_list variable with the ... after fmt */
    va_start(myargs, fmt);

    /* Forward the '...' to printf with the correct start */
    printf(ERROR_TEXT, current_timestmap);
    ret = vprintf(fmt, myargs);

    /* Clean up the va_list */
    va_end(myargs);

    // Free timestamp
    free(current_timestmap);

    return ret;
}

int logger_warn(char *fmt, ...)
{
    int ret;
    va_list myargs;

    char *current_timestmap = get_current_timestamp();

    /* Initialise the va_list variable with the ... after fmt */
    va_start(myargs, fmt);

    /* Forward the '...' to printf with the correct start */
    printf(WARN_TEXT, current_timestmap);
    ret = vprintf(fmt, myargs);

    /* Clean up the va_list */
    va_end(myargs);

    // Free timestamp
    free(current_timestmap);

    return ret;
}

int logger_info(char *fmt, ...)
{
    int ret;
    va_list myargs;

    char *current_timestmap = get_current_timestamp();

    /* Initialise the va_list variable with the ... after fmt */
    va_start(myargs, fmt);

    /* Forward the '...' to printf with the correct start */
    printf(INFO_TEXT, current_timestmap);
    ret = vprintf(fmt, myargs);

    /* Clean up the va_list */
    va_end(myargs);

    // Free timestamp
    free(current_timestmap);

    return ret;
}

int logger_debug(char *fmt, ...)
{
    int ret;
    va_list myargs;

    char *current_timestmap = get_current_timestamp();

    /* Initialise the va_list variable with the ... after fmt */
    va_start(myargs, fmt);

    /* Forward the '...' to printf with the correct start */
    printf(DEBUG_TEXT, current_timestmap);
    ret = vprintf(fmt, myargs);

    /* Clean up the va_list */
    va_end(myargs);

    // Free timestamp
    free(current_timestmap);

    return ret;
}
