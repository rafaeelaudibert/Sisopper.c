CC=gcc
FLAGS=-Wall -Wpedantic -g -pthread -Iinclude/client -Iinclude/server -Iinclude/structures -Iinclude/utils
LIBRARIES=-lncurses

BIN_FOLDER=./bin
SERVER_BIN=${BIN_FOLDER}/server
CLIENT_BIN=${BIN_FOLDER}/client
FRONT_END_BIN=${BIN_FOLDER}/front_end

all: server client front_end
	@echo "Done!"

# On release, remove debug, activate O2 optimization and removes debug prints
release: FLAGS += -g0 -O2 -D NO_DEBUG
release: all

# Server related
server: server.o chained_list.o logger.o hash.o savefile.o user.o server_ring.o socket.o
	${CC} ${FLAGS} -o ${SERVER_BIN} server.o chained_list.o logger.o hash.o savefile.o user.o server_ring.o socket.o ${LIBRARIES}

server.o: src/server/server.c
	${CC} ${FLAGS} -c src/server/server.c

savefile.o: src/server/savefile.c
	${CC} ${FLAGS} -c src/server/savefile.c

# FE related
front_end: front_end.o chained_list.o logger.o hash.o savefile.o user.o server_ring.o socket.o
	${CC} ${FLAGS} -o ${FRONT_END_BIN} front_end.o chained_list.o logger.o hash.o savefile.o user.o server_ring.o socket.o ${LIBARIES}

front_end.o: src/FE/front_end.c
	${CC} ${FLAGS} -c src/FE/front_end.c


# Server + FE
server_ring.o: src/server/server_ring.c
	${CC} ${FLAGS} -c src/server/server_ring.c


# Client related
client: client.o logger.o hash.o ui.o chained_list.o
	${CC} ${FLAGS} -o ${CLIENT_BIN} client.o logger.o  hash.o ui.o chained_list.o ${LIBRARIES}

client.o: src/client/client.c
	${CC} ${FLAGS} -c src/client/client.c

ui.o: src/client/ui.c
	${CC} ${FLAGS} -c src/client/ui.c

# Structures
hash.o: src/structures/hash.c
	${CC} ${FLAGS} -c src/structures/hash.c

user.o: src/structures/user.c
	${CC} ${FLAGS} -c src/structures/user.c

chained_list.o: src/structures/chained_list.c
	${CC} ${FLAGS} -c src/structures/chained_list.c

socket.o: src/structures/socket.c
	${CC} ${FLAGS} -c src/structures/socket.c

# Utilities
logger.o: src/utils/logger.c
	${CC} ${FLAGS} -c src/utils/logger.c

# Clear
clear:
	rm ${SERVER_BIN} ${CLIENT_BIN} ${FRONT_END_BIN} *.o

# Remove the savefile
clear_savefile:
savefile:
	@rm .savefile
