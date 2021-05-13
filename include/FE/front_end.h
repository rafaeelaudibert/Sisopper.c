#ifndef FRONT_END
#define FRONT_END


int FE_PORTS[] = {12001, 12002, 12003, 12004, 12005};
char *FE_HOSTS[] = {"127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1"};
#define NUMBER_OF_FES (sizeof(FE_PORTS) / sizeof(FE_PORTS[0]))

#endif
