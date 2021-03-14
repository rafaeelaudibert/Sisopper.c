CC=gcc
FLAGS=-Wall -O2 -g -pthread

BIN_FOLDER=./bin
SERVER_BIN=${BIN_FOLDER}/server
CLIENT_BIN=${BIN_FOLDER}/client

all: server client
	- echo "Done!"

release: FLAGS += -D NO_DEBUG
release: all

# Server related
server: server.o chained_list.o logger.o
	${CC} ${FLAGS} -o ${SERVER_BIN} server.o chained_list.o logger.o

server.o:
	${CC} ${FLAGS} -c server.c

# Client related
client: client.o logger.o
	${CC} ${FLAGS} -o ${CLIENT_BIN} client.o logger.o

client.o:
	${CC} ${FLAGS} -c client.c

# Hash table Data Structure
hash.o: hash.c
	${CC} ${FLAGS} -c hash.c

# Chained List Data Structure
chained_list.o: chained_list.c
	${CC} ${FLAGS} -c chained_list.c

# Utilities
logger.o: logger.c
	${CC} ${FLAGS} -c logger.c

# Clear
clear:
	rm ${BIN_FOLDER}/server ${BIN_FOLDER}/client *.o