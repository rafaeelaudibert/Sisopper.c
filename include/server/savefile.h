#ifndef SAVEFILE_H_
#define SAVEFILE_H_

#include "hash.h"

HASH_TABLE read_savefile(void);
void save_savefile(HASH_TABLE);

#endif // SAVEFILE_H_
