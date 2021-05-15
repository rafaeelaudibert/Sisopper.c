# Sisopper 🐧
 
Twitter-like application with serveral servers and several clients communicating through UNIX sockets (with some frontend applications in the middle!).

## Usage 💬

You can compile the aplication running `make`. This application was only tested on Linux (Ubuntu and Deepin).
Also, `<ncurses.h>` is required to compile this project.

### Running the server 📁

The server can be run with `bin/server` and it will listen on the first available port

### Running the frontend 🔀
The server can be run with `bin/front_end` and it will listen on the first available port, and automatically try to connect to the server

### Running the client 📱

A client can be run with `bin/client @handle` which will automatically connect to its corresponding `front_end`. The front_end is chosen based on an `@handle` hash

## Authors 🧙

* [Ana Carolina Pagnoncelli](https://github.com/Ana2877)
* [Astelio José Weber](https://github.com/TeoWeber)
* [Giovanna Varella Damian](https://github.com/gvdamian)
* [Rafael Baldasso Audibert](https://www.rafaaudibert.dev)