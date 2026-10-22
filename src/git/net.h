/*
 * net.h - network sockets
 *
 * A thin layer over Winsock (Windows) and BSD sockets (everything else):
 * just enough for a web server that never waits.
 */

#ifndef MINIGITHUB_NET_H
#define MINIGITHUB_NET_H

#include "common.h"

/* The socket type and its "no socket" value differ between Windows and the rest. */
#ifdef _WIN32
typedef uintptr_t Socket;
#define NO_SOCKET (~(uintptr_t)0)
#else
typedef int Socket;
#define NO_SOCKET (-1)
#endif

/* Gets the network functions ready (once). False if they can't be loaded. */
bool NetworkInit(void);

/* Listens on a port of 127.0.0.1 (this computer only), without blocking. NO_SOCKET if the port is
   taken. */
Socket SocketListen(int port);

/* A new connection waiting on listener (set not to block), or NO_SOCKET if there isn't one. */
Socket SocketAccept(Socket listener);

/* Receives up to capacity bytes: how many arrived (>0), 0 if the other side closed the
   connection, -1 if nothing has arrived yet, -2 if the connection is broken. */
int  SocketReceive(Socket sock, uint8_t *buffer, int capacity);

/* Sends up to count bytes: how many went (>0), -1 if none could go yet, -2 if the connection is
   broken. */
int  SocketSend(Socket sock, const uint8_t *bytes, int count);

/* Closes a socket. */
void SocketClose(Socket sock);

#endif
