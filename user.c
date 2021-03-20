#include "user.h"

HASH_USER *user_table[HASH_SIZE];

void hashInit(void){
  int i;
  for(i=0; i<HASH_SIZE; i++)
    user_table[i]=0;
}

int hashAddress(char *username){
  int address = 1;
  int i;
  for(i=0; i<strlen(username); i++){
    address = (address * username[i]) % HASH_SIZE + 1;
  }
  return address -1;
}

HASH_USER *hashFind(char *username){
  HASH_USER *user;
  int address = hashAddress(username);
  for(user=user_table[address]; user; user = user->next){
    if(strcmp(user->username, username) == 0)
      return user;
  }

  return 0;
}

HASH_USER *hashInsert(char *username, USER user){
  HASH_USER *new_user;
  int address = hashAddress(username);

  if((new_user = hashFind(username)) !=0)
    return new_user;
  new_user = (HASH_USER *) calloc(1, sizeof(HASH_USER));
  new_user->user = user;
  strcpy(new_user->username, username);
  new_user->next = user_table[address];
  user_table[address] = new_user;
  return new_user;
}

void hashPrint(void){
  int i;
  HASH_USER *user;
  for(i=0; i<HASH_SIZE; i++)
    for (user=user_table[i]; user; user=user->next)
      printf("user_table[%d] has %s\n", i, user->username);
}
