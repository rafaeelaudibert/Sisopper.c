#ifndef LOGGER_H
#define LOGGER_H

#define ERROR_TEXT "🚨 [ERROR] (%s) "
#define WARN_TEXT "⚠️  [WARN] (%s) "
#define INFO_TEXT "ℹ️   [INFO] (%s) "
#define DEBUG_TEXT "🐛 [DEBUG] (%s) "

char *get_current_timestamp();
int logger_error(char *fmt, ...);
int logger_warn(char *fmt, ...);
int logger_info(char *fmt, ...);
int logger_debug(char *fmt, ...);

#endif // LOGGER_H
