# Sisopper 🐧
 
WIP Twitter-like application with a server and several clients communicating through UNIX sockets.

## Usage

You can compile the aplication running `make`. This application was only tested on Linux (Ubuntu and Deepin).
Also, `<ncurses.h>` is required to compile this project.

### Running the server

The server can be run with `bin/server` and will listen in the port `4000`

### Running the client

A client can be run with `bin/client @handle` which will automatically connect to the `server` running on port `4000`


## Authors 🧙

* [Ana Carolina Pagnoncelli](https://github.com/Ana2877)
* [Astelio José Weber](https://github.com/TeoWeber)
* [Giovanna Varella Damian](https://github.com/gvdamian)
* [Rafael Baldasso Audibert](https://www.rafaaudibert.dev)