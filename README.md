# Sisopper ð§
 
Twitter-like application with serveral servers and several clients communicating through UNIX sockets (with some frontend applications in the middle!).

## Usage ð¬

You can compile the aplication running `make`. This application was only tested on Linux (Ubuntu and Deepin).
Also, `<ncurses.h>` is required to compile this project.

### Running the server ð

The server can be run with `bin/server` and it will listen on the first available port

### Running the frontend ð
The server can be run with `bin/front_end` and it will listen on the first available port, and automatically try to connect to the server

### Running the client ð±

A client can be run with `bin/client @handle` which will automatically connect to its corresponding `front_end`. The front_end is chosen based on an `@handle` hash

## Authors ð§

* [Ana Carolina Pagnoncelli](https://github.com/Ana2877)
* [Astelio JosÃ© Weber](https://github.com/TeoWeber)
* [Giovanna Varella Damian](https://github.com/gvdamian)
* [Rafael Baldasso Audibert](https://www.rafaaudibert.dev)