/*
 * net.c - network sockets
 *
 * A thin layer over Winsock (Windows) and BSD sockets (everything else):
 * just enough for a web server that never waits.
 */

#include "net.h"

/* ---------- network connections ----------
   On Windows the network functions live in ws2_32.dll, which is loaded
   while the program runs - so building Mini GitHub needs nothing extra. */
#ifdef _WIN32
struct Ipv4Address { unsigned short family, port; uint32_t address; char padding[8]; };
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
static int  (__stdcall *winsockStartup)(unsigned short, void *);
static Socket (__stdcall *winsockSocket)(int, int, int);
static int  (__stdcall *winsockBind)(Socket, const void *, int);
static int  (__stdcall *winsockListen)(Socket, int);
static Socket (__stdcall *winsockAccept)(Socket, void *, int *);
static int  (__stdcall *winsockRecv)(Socket, char *, int, int);
static int  (__stdcall *winsockSend)(Socket, const char *, int, int);
static int  (__stdcall *winsockClose)(Socket);
static int  (__stdcall *winsockIoctl)(Socket, long, unsigned long *);
static int  (__stdcall *winsockLastError)(void);
/* Gets the network functions ready (once). False if they can't be loaded. */
bool NetworkInit(void)
{
    static int loadState;                               /* 0 not tried, 1 ready, -1 failed */
    if (loadState) return loadState > 0;
    loadState = -1;
    void *winsock = LoadLibraryA("ws2_32.dll");
    if (!winsock) return false;
    *(void **)&winsockStartup = GetProcAddress(winsock, "WSAStartup");
    *(void **)&winsockSocket  = GetProcAddress(winsock, "socket");
    *(void **)&winsockBind    = GetProcAddress(winsock, "bind");
    *(void **)&winsockListen  = GetProcAddress(winsock, "listen");
    *(void **)&winsockAccept  = GetProcAddress(winsock, "accept");
    *(void **)&winsockRecv    = GetProcAddress(winsock, "recv");
    *(void **)&winsockSend    = GetProcAddress(winsock, "send");
    *(void **)&winsockClose   = GetProcAddress(winsock, "closesocket");
    *(void **)&winsockIoctl   = GetProcAddress(winsock, "ioctlsocket");
    *(void **)&winsockLastError   = GetProcAddress(winsock, "WSAGetLastError");
    if (!winsockStartup || !winsockSocket || !winsockBind || !winsockListen || !winsockAccept || !winsockRecv || !winsockSend || !winsockClose || !winsockIoctl || !winsockLastError) return false;
    static uint8_t wsaData[1024];
    if (winsockStartup(0x0202, wsaData) != 0) return false;
    loadState = 1;
    return true;
}
/* Listens on a port of 127.0.0.1 (this computer only), without blocking. NO_SOCKET if the port is
   taken. */
Socket SocketListen(int port)
{
    Socket sock = winsockSocket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 6 /* TCP */);
    if (sock == NO_SOCKET) return NO_SOCKET;
    struct Ipv4Address address = {2, (unsigned short)((port >> 8) | ((port & 0xFF) << 8)), 0x0100007Fu, {0}};  /* 127.0.0.1 */
    unsigned long nonBlocking = 1;
    if (winsockBind(sock, &address, (int)sizeof address) != 0 || winsockListen(sock, 16) != 0 || winsockIoctl(sock, (long)0x8004667Eu /* FIONBIO */, &nonBlocking) != 0) { winsockClose(sock); return NO_SOCKET; }
    return sock;
}
/* A new connection waiting on listener (set not to block), or NO_SOCKET if there isn't one. */
Socket SocketAccept(Socket listener)
{
    Socket sock = winsockAccept(listener, NULL, NULL);
    if (sock == NO_SOCKET) return NO_SOCKET;
    unsigned long nonBlocking = 1;
    winsockIoctl(sock, (long)0x8004667Eu, &nonBlocking);
    return sock;
}
/* Receives up to capacity bytes: how many arrived (>0), 0 if the other side closed the
   connection, -1 if nothing has arrived yet, -2 if the connection is broken. */
int SocketReceive(Socket sock, uint8_t *buffer, int capacity)
{
    int result = winsockRecv(sock, (char *)buffer, capacity, 0);
    if (result > 0) return result;
    if (result == 0) return 0;
    return winsockLastError() == 10035 /* WSAEWOULDBLOCK */ ? -1 : -2;
}
/* Sends up to count bytes: how many went (>0), -1 if none could go yet, -2 if the connection is
   broken. */
int SocketSend(Socket sock, const uint8_t *bytes, int count)
{
    int result = winsockSend(sock, (const char *)bytes, count, 0);
    if (result >= 0) return result;
    return winsockLastError() == 10035 ? -1 : -2;
}
/* Closes a socket. */
void SocketClose(Socket sock) { winsockClose(sock); }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
bool NetworkInit(void)
{
    signal(SIGPIPE, SIG_IGN);                            /* a client hanging up mustn't stop the program */
    return true;
}
Socket SocketListen(int port)
{
    Socket sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NO_SOCKET;
    int nonBlocking = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &nonBlocking, sizeof nonBlocking);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(0x7F000001u);        /* 127.0.0.1: this computer only */
    if (bind(sock, (struct sockaddr *)&address, sizeof address) != 0 || listen(sock, 16) != 0) { close(sock); return NO_SOCKET; }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    return sock;
}
Socket SocketAccept(Socket listener)
{
    Socket sock = accept(listener, NULL, NULL);
    if (sock < 0) return NO_SOCKET;
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    int nonBlocking = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &nonBlocking, sizeof nonBlocking);
#endif
    return sock;
}
int SocketReceive(Socket sock, uint8_t *buffer, int capacity)
{
    ssize_t result = recv(sock, buffer, (size_t)capacity, 0);
    if (result > 0) return (int)result;
    if (result == 0) return 0;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -1 : -2;
}
int SocketSend(Socket sock, const uint8_t *bytes, int count)
{
    ssize_t result = send(sock, bytes, (size_t)count, 0);
    if (result >= 0) return (int)result;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -1 : -2;
}
void SocketClose(Socket sock) { close(sock); }
#endif
