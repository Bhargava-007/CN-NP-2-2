/* server_iocp.c - CP2, model 4 on Windows: I/O completion ports (IOCP).
   Linux has epoll (readiness: "you may read now"); Windows has IOCP (completion: "your read finished").
   You post an overlapped WSARecv per connection; worker threads wake only for connections that
   actually received data - O(active), not O(all) like select/WSAPoll.
   Run:  server_iocp.exe 8080          (Windows only)                                            */
#ifndef _WIN32
#include <stdio.h>
int main(void) { puts("server_iocp is Windows-only; on Linux the equivalent model is epoll."); return 0; }
#else
#include "platform.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFSZ 4096
static const char RESP[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: keep-alive\r\n\r\nok\n";
static volatile LONG g_open = 0, g_served = 0;
static HANDLE g_iocp;

typedef struct {
    OVERLAPPED ov;               /* MUST be first: the completion hands back this pointer */
    SOCKET fd;
    WSABUF wb;
    char rx[BUFSZ];              /* where the kernel writes the received bytes            */
    char acc[BUFSZ + 1];         /* request accumulator (bytes across completions)         */
    size_t len;
} ctx;

static void post_recv(ctx *c) {
    DWORD flags = 0;
    ZeroMemory(&c->ov, sizeof c->ov);
    c->wb.buf = c->rx; c->wb.len = BUFSZ;
    if (WSARecv(c->fd, &c->wb, 1, NULL, &flags, &c->ov, NULL) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        closesocket(c->fd); free(c); InterlockedDecrement(&g_open);
    }
}

static void handle(ctx *c, DWORD n) {
    if (c->len + n > BUFSZ) c->len = 0;
    memcpy(c->acc + c->len, c->rx, n); c->len += n; c->acc[c->len] = '\0';
    char *e;
    while ((e = strstr(c->acc, "\r\n\r\n")) != NULL) {          /* pipelined requests */
        send(c->fd, RESP, (int)(sizeof RESP - 1), 0);
        size_t used = (size_t)(e + 4 - c->acc);
        memmove(c->acc, c->acc + used, c->len - used + 1); c->len -= used;
        InterlockedIncrement(&g_served);
    }
}

static DWORD WINAPI worker(LPVOID unused) {
    (void)unused;
    for (;;) {
        DWORD n = 0; ULONG_PTR key = 0; OVERLAPPED *ov = NULL;
        BOOL ok = GetQueuedCompletionStatus(g_iocp, &n, &key, &ov, INFINITE);  /* sleeps until a read completes */
        if (!ov) continue;
        ctx *c = (ctx *)ov;
        if (!ok || n == 0) { closesocket(c->fd); free(c); InterlockedDecrement(&g_open); continue; }  /* FIN or error */
        handle(c, n);
        post_recv(c);                                              /* ask for the next read */
    }
    return 0;
}

static DWORD WINAPI reporter(LPVOID unused) {
    (void)unused;
    for (;;) { Sleep(5000); printf("open=%ld served=%ld\n", g_open, g_served); fflush(stdout); }
    return 0;
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (net_init()) return 1;
    SYSTEM_INFO si; GetSystemInfo(&si);
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    for (DWORD i = 0; i < si.dwNumberOfProcessors; i++) CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, reporter, NULL, 0, NULL));

    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, SOMAXCONN)) { fprintf(stderr, "bind/listen failed %d\n", WSAGetLastError()); return 1; }
    printf("model=iocp port=%d workers=%lu\n", port, si.dwNumberOfProcessors);
    for (;;) {
        SOCKET s = accept(ls, NULL, NULL);
        if (s == INVALID_SOCKET) continue;
        ctx *c = calloc(1, sizeof *c); c->fd = s;
        CreateIoCompletionPort((HANDLE)s, g_iocp, (ULONG_PTR)c, 0);   /* bind the socket to the port once */
        InterlockedIncrement(&g_open);
        post_recv(c);
    }
}
#endif
