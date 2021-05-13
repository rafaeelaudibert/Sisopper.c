#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "logger.h"
#include "user.h"
#include "savefile.h"

HASH_TABLE read_savefile()
{
    FILE *savefile = fopen(SAVEFILE_FILE_PATH, "r");
    if (!savefile) // File doesn't exist
    {
        logger_warn("Savefile doesn't exist. Returning empty user hash\n");
        return hash_init();
    }

    fseek(savefile, 0, SEEK_END);
    long size = ftell(savefile);
    if (size == 0) // File is empty
    {
        logger_warn("Savefile existed but it was empty. Returning empty user hash\n");
        return hash_init();
    }
    fseek(savefile, 0, SEEK_SET); // Go back to start

    logger_debug("Savefile exists, reading from it!\n");

    // It will break if we have more than 80 followers for this guy
    int MAX_USERNAME_SIZE = MAX_USERNAME_LENGTH + 5,
        MAX_FOLLOWERS_SIZE = MAX_USERNAME_SIZE * 80;

    char *username = (char *)calloc(MAX_USERNAME_SIZE, sizeof(char)),
         *followers = (char *)calloc(MAX_FOLLOWERS_SIZE, sizeof(char)),
         *follower = (char *)calloc(MAX_USERNAME_SIZE, sizeof(char));
    HASH_TABLE table = hash_init();
    while (fgets(username, MAX_USERNAME_SIZE, savefile))
    {
        // We are in the last line, so we finished it
        if (username[0] == '\n')
            break;

        if (fgets(followers, MAX_FOLLOWERS_SIZE, savefile) == NULL)
        {
            logger_warn("Error reading followers list for %s\n", username);
        };

        // Remove \n
        username[strcspn(username, "\n")] = 0;
        followers[strcspn(followers, "\n")] = 0;
        logger_debug("%s is followed by %s\n", username, followers);

        USER *user = init_user();

        follower = strtok(followers, ",");
        while (follower != NULL)
        {
            user->followers = chained_list_append_end(user->followers, (void *)strdup(follower));
            follower = strtok(NULL, ",");
        }

        strcpy(user->username, username);

        hash_insert(table, strdup(username), (void *)user);
    }

    // Remember to close the file
    fclose(savefile);

    logger_info("Savefile successfully loaded\n");
    return table;
}

void save_savefile(HASH_TABLE table)
{

    if (!table)
        return;

    FILE *savefile = fopen(SAVEFILE_FILE_PATH, "w");

    int list_idx;
    HASH_NODE *node;
    USER *user;
    char *username;
    CHAINED_LIST *follower_list;
    for (int table_idx = 0; table_idx < HASH_SIZE; table_idx++)
        for (node = table[table_idx], list_idx = 0; node; node = node->next, list_idx++)
        {
            user = (USER *)node->value;

            username = node->key;
            fprintf(savefile, "%s\n", username);

            follower_list = user->followers;
            while (follower_list)
            {
                fprintf(savefile, "%s,", (char *)follower_list->val);
                follower_list = follower_list->next;
            }
            fprintf(savefile, "\n");

            logger_info("Saved user %s to savefile...\n", username);
        }

    // Remember to close file
    fclose(savefile);
}
