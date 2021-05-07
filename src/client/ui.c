#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "ui.h"
#include "chained_list.h"

typedef struct
{
    chtype ls, rs, ts, bs, tl, tr, bl, br;
} WIN_BORDER;

typedef struct
{
    int startx, starty;
    int height, width;
    WIN_BORDER border;
} WINDOW_OPTIONS;

void clear_chatbox();
void clear_timeline();
void init_windows(char *username);
void init_chatbox_options(WINDOW_OPTIONS *);
void init_timeline_options(WINDOW_OPTIONS *);
WINDOW *create_newwin(WINDOW_OPTIONS);
void update_timeline(void *);

WINDOW_OPTIONS chatbox_options, timeline_options;
WINDOW *chatbox = NULL, *timeline = NULL;

pthread_t update_timeline_tid = -1;

CHAINED_LIST *messages_list = NULL;

pthread_mutex_t SYNC_MUTEX = PTHREAD_MUTEX_INITIALIZER;     /* mutex for sync display */
pthread_mutex_t MESSAGES_MUTEX = PTHREAD_MUTEX_INITIALIZER; /* mutex for messages list */

#define LOCK_SCREEN pthread_mutex_lock(&SYNC_MUTEX)
#define UNLOCK_SCREEN pthread_mutex_unlock(&SYNC_MUTEX)

#define LOCK_MESSAGES_LIST pthread_mutex_lock(&MESSAGES_MUTEX)
#define UNLOCK_MESSAGES_LIST pthread_mutex_unlock(&MESSAGES_MUTEX)

void UI_start(char *username)
{
    initscr();     // Start curses mode
    start_color(); // Start the color functionality
    cbreak();      // Line buffering disabled
    noecho();
    refresh();

    // Configure every window
    init_windows(username);

    // Configure thread which prints messages
    pthread_create(&update_timeline_tid, NULL, (void *(*)(void *)) & update_timeline, NULL);
}

void UI_end(void)
{
    mvwaddstr(chatbox, CHATBOX_HEIGHT - 1, 4, "FINISHING! PRESS <ESC> TO EXIT.");
    wrefresh(chatbox);
    while (getch() != 27) // ESC KEY
    {
    }

    pthread_cancel(update_timeline_tid);

    endwin(); // End curses mode
}

char *UI_get_text(unsigned int max_size)
{
    int CHARACTER_SIZE_BUFFER_SIZE = 20;

    char *buffer = (char *)calloc(max_size + 1, sizeof(char));
    char *character_size_buffer = (char *)calloc(CHARACTER_SIZE_BUFFER_SIZE, sizeof(char)); // This allows at most 9999 chars, more than that we will have a segflow
    int idx = 0;
    int is_full = false;

    while (true)
    {
        LOCK_SCREEN;

        // Clear the chatbox
        clear_chatbox();

        // Print how many characters we are using out of the max
        sprintf(character_size_buffer, "%04d/%04d", idx, max_size);
        mvwaddstr(chatbox, CHATBOX_HEIGHT - 1, COLS - CHARACTER_SIZE_BUFFER_SIZE - 1, character_size_buffer);

        // Print the buffer as a return to the user
        mvwaddstr(chatbox, 2, 4, buffer);

        // Refresh screen
        wrefresh(chatbox);

        UNLOCK_SCREEN;

        char c = getch();
        if (c == 10) // Enter
        {
            clear_chatbox();
            break;
        }
        else if (c == 127) // Backspace
        {
            buffer[--idx] = '\0';
            is_full = false;
            if (idx < 0)
                idx = 0;
        }
        else if (!is_full)
        {
            buffer[idx++] = c;
            is_full = idx == max_size;
        }
    }

    return buffer;
}

void UI_add_new_message(UI_MESSAGE *ui_message)
{
    LOCK_MESSAGES_LIST;
    messages_list = chained_list_append_start(messages_list, (void *)ui_message);
    UNLOCK_MESSAGES_LIST;
}

/* PRIVATE */

void update_timeline(void *_arg)
{
    CHAINED_LIST *old_messages_list = NULL;
    struct timespec sleep_config = {
        .tv_sec = 0,
        .tv_nsec = 5000000, // 5ms
    };

    while (1)
    {
        nanosleep(&sleep_config, &sleep_config);

        LOCK_MESSAGES_LIST;
        if (old_messages_list == messages_list)
        {
            UNLOCK_MESSAGES_LIST;
            continue;
        }

        // Clear timeline screen to draw messages on top of it
        clear_timeline();

        LOCK_SCREEN;
        CHAINED_LIST *message = messages_list;
        int lines_left = CHATBOX_START_Y - 3;
        while (lines_left > 5 && message) // Give us some space in the last one
        {
            UI_MESSAGE *ui_message = (UI_MESSAGE *)message->val;
            if (ui_message->type == UI_MESSAGE_TYPE__MESSAGE)
            {
                char *header_message = (char *)calloc(100, sizeof(char));
                sprintf(header_message, "%s - [%lld]", ui_message->author, (long long)ui_message->timestamp);
                mvwaddstr(timeline, CHATBOX_START_Y - lines_left, 3, header_message);

                mvwaddstr(timeline, CHATBOX_START_Y - lines_left + 1, 3, ui_message->message);

                lines_left -= 5;
            }
            else if (ui_message->type == UI_MESSAGE_TYPE__INFO)
            {
                char *header_message = (char *)calloc(220, sizeof(char));
                sprintf(header_message, "[INFO] | %s - [%lld]", ui_message->message, (long long)ui_message->timestamp);
                mvwaddstr(timeline, CHATBOX_START_Y - lines_left, 3, header_message);

                lines_left -= 3;
            }
            wrefresh(timeline);

            message = message->next;
        }
        wrefresh(timeline);
        UNLOCK_SCREEN;

        old_messages_list = messages_list;
        UNLOCK_MESSAGES_LIST;

        wrefresh(timeline);
    }
}

void init_windows(char *username)
{
    /* Initialize the window parameters */
    init_timeline_options(&timeline_options);
    init_chatbox_options(&chatbox_options);

    /* Draw the windows */
    timeline = create_newwin(timeline_options);
    chatbox = create_newwin(chatbox_options);

    /* Write the boxes titles */

    clear_timeline();
    clear_chatbox();

    /* Write the user handle and prompt */
    mvwprintw(chatbox, 1, 2, username);
    mvwprintw(chatbox, 1, 2 + strlen(username) + 1, "says:");

    wrefresh(timeline);
    wrefresh(chatbox);
    refresh();
}

void clear_chatbox()
{
    wmove(chatbox, 2, 1);
    wclrtoeol(chatbox);

    wmove(chatbox, 3, 1);
    wclrtoeol(chatbox);

    wborder(chatbox,
            chatbox_options.border.ls,
            chatbox_options.border.rs,
            chatbox_options.border.ts,
            chatbox_options.border.bs,
            chatbox_options.border.tl,
            chatbox_options.border.tr,
            chatbox_options.border.bl,
            chatbox_options.border.br);

    mvwprintw(chatbox, 0, 3, "\\ CHATBOX /");
}

void clear_timeline()
{
    for (int i = 2; i < CHATBOX_START_Y; i++)
    {
        wmove(timeline, i, 1);
        wclrtoeol(timeline);
    }

    wborder(timeline,
            timeline_options.border.ls,
            timeline_options.border.rs,
            timeline_options.border.ts,
            timeline_options.border.bs,
            timeline_options.border.tl,
            timeline_options.border.tr,
            timeline_options.border.bl,
            timeline_options.border.br);

    mvwprintw(timeline, 0, 3, "\\ TIMELINE /");
}

void init_chatbox_options(WINDOW_OPTIONS *chatbox_options)
{
    chatbox_options->height = CHATBOX_HEIGHT;
    chatbox_options->width = COLS;
    chatbox_options->startx = 0;
    chatbox_options->starty = CHATBOX_START_Y;

    chatbox_options->border.ls = '|';
    chatbox_options->border.rs = '|';
    chatbox_options->border.ts = '-';
    chatbox_options->border.bs = '-';
    chatbox_options->border.tl = '+';
    chatbox_options->border.tr = '+';
    chatbox_options->border.bl = '+';
    chatbox_options->border.br = '+';
}

void init_timeline_options(WINDOW_OPTIONS *timeline_options)
{
    timeline_options->height = LINES - CHATBOX_HEIGHT;
    timeline_options->width = COLS;
    timeline_options->startx = 0;
    timeline_options->starty = 0;

    timeline_options->border.ls = '|';
    timeline_options->border.rs = '|';
    timeline_options->border.ts = '-';
    timeline_options->border.bs = '-';
    timeline_options->border.tl = '+';
    timeline_options->border.tr = '+';
    timeline_options->border.bl = '+';
    timeline_options->border.br = '+';
}

WINDOW *create_newwin(WINDOW_OPTIONS options)
{
    WINDOW *win;

    win = newwin(options.height, options.width, options.starty, options.startx);
    wborder(win,
            options.border.ls,
            options.border.rs,
            options.border.ts,
            options.border.bs,
            options.border.tl,
            options.border.tr,
            options.border.bl,
            options.border.br);

    // Show the window and their box
    wrefresh(win);

    return win;
}