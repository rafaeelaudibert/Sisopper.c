CC=gcc
FLAGS=-Wall -g -pthread -Iinclude
LIBRARIES=-lncurses

BIN_FOLDER=./bin
SERVER_BIN=${BIN_FOLDER}/server
CLIENT_BIN=${BIN_FOLDER}/client

all: server client
	@echo "Done!"

# On release, remove debug, activate O2 optimization and removes debug prints
release: FLAGS += -g0 -O2 -D NO_DEBUG
release: all

# Server related
server: server.o chained_list.o logger.o hash.o savefile.o user.o
	${CC} ${FLAGS} -o ${SERVER_BIN} server.o chained_list.o logger.o hash.o savefile.o user.o ${LIBRARIES}

server.o: src/server.c
	${CC} ${FLAGS} -c src/server.c

# Client related
client: client.o logger.o  hash.o ui.o chained_list.o
	${CC} ${FLAGS} -o ${CLIENT_BIN} client.o logger.o  hash.o ui.o chained_list.o ${LIBRARIES}

client.o: src/client.c
	${CC} ${FLAGS} -c src/client.c

ui.o: src/ui.c
	${CC} ${FLAGS} -c src/ui.c

# Hash table Data Structure
hash.o: src/hash.c
	${CC} ${FLAGS} -c src/hash.c

savefile.o: src/savefile.c
	${CC} ${FLAGS} -c src/savefile.c

user.o: src/user.c
	${CC} ${FLAGS} -c src/user.c

# Chained List Data Structure
chained_list.o: src/chained_list.c
	${CC} ${FLAGS} -c src/chained_list.c

# Utilities
logger.o: src/logger.c
	${CC} ${FLAGS} -c src/logger.c

# Clear
clear:
	rm ${BIN_FOLDER}/server ${BIN_FOLDER}/client *.o

# Remove the savefile
clear_savefile:
savefile:
	@rm .savefile
