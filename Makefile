CC=gcc
FLAGS=-Wall -O2 -g

BIN_FOLDER=./bin
SERVER_BIN=${BIN_FOLDER}/server
CLIENT_BIN=${BIN_FOLDER}/client

all: server client
	- echo "Done!"

# Server related
server: server.o
	${CC} ${FLAGS} -o ${SERVER_BIN} server.o

server.o:
	${CC} ${FLAGS} -c server.c

# Client related
client: client.o
	${CC} ${FLAGS} -o ${CLIENT_BIN} client.o

client.o:
	${CC} ${FLAGS} -c client.c

# Hash table
hash.o: hash.c
	${CC} ${FLAGS} -c hash.c

# Utilities
clear:
	rm ${BIN_FOLDER}/server ${BIN_FOLDER}/client *.o