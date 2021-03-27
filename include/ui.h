#ifndef UI_H_
#define UI_H_

#include <ncurses.h>

#define CHATBOX_HEIGHT 6
#define CHATBOX_START_Y ((LINES) - (CHATBOX_HEIGHT))

typedef enum
{
    UI_MESSAGE_TYPE__MESSAGE,
    UI_MESSAGE_TYPE__INFO
} UI_MESSAGE_TYPE;

typedef struct
{
    char *author;
    char *message;
    time_t timestamp;
    u_int8_t is_fatal;
    UI_MESSAGE_TYPE type;
} UI_MESSAGE;

void UI_start(char *);
void UI_end(void);
char *UI_get_text(unsigned int);
void UI_add_new_message(UI_MESSAGE *);

#endif // UI_H_