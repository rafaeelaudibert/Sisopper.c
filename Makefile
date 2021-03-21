CC=gcc
FLAGS=-Wall -g -pthread

BIN_FOLDER=./bin
SERVER_BIN=${BIN_FOLDER}/server
CLIENT_BIN=${BIN_FOLDER}/client

all: server client
	- echo "Done!"

# On release, remove debug, activate O2 optimization and removes debug prints
release: FLAGS += -g0 -O2 -D NO_DEBUG
release: all

# Server related
server: server.o chained_list.o logger.o user.o
	${CC} ${FLAGS} -o ${SERVER_BIN} server.o chained_list.o logger.o user.o

server.o:
	${CC} ${FLAGS} -c server.c

# Client related
client: client.o logger.o  user.o
	${CC} ${FLAGS} -o ${CLIENT_BIN} client.o logger.o  user.o

client.o:
	${CC} ${FLAGS} -c client.c

# Hash table Data Structure
user.o: chained_list.o user.c
	${CC} ${FLAGS} -c user.c

# Chained List Data Structure
chained_list.o: chained_list.c
	${CC} ${FLAGS} -c chained_list.c

# Utilities
logger.o: logger.c
	${CC} ${FLAGS} -c logger.c

# Clear
clear:
	rm ${BIN_FOLDER}/server ${BIN_FOLDER}/client *.o
