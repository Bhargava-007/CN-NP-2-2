/* webget.c - CP0 part 1b: fetch a page over a kernel TCP socket (Winsock on Windows).
   Usage:  webget.exe <host> <path> [port]        e.g.  webget.exe localhost / 8080       */
#include "platform.h"
#include <stdio.h>
#include <string.h>

static int send_all(sock_t s, const char *p, size_t n) {
    while (n > 0) {                               /* send() may accept fewer bytes than offered */
        int k = send(s, p, (int)n, 0);
        if (k <= 0) return -1;
        p += k; n -= (size_t)k;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <host> <path> [port]\n", argv[0]); return 2; }
    const char *host = argv[1], *path = argv[2], *port = argc > 3 ? argv[3] : "80";
    if (net_init() != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);          /* DNS lookup (week 4 does this by hand) */
    if (rc != 0) { fprintf(stderr, "getaddrinfo: %d\n", rc); net_cleanup(); return 1; }

    sock_t s = SETU_BAD_SOCK;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == SETU_BAD_SOCK) continue;
        if (connect(s, a->ai_addr, (sock_len_t)a->ai_addrlen) == 0) break;   /* three-way handshake */
        sock_close(s); s = SETU_BAD_SOCK;
    }
    freeaddrinfo(res);
    if (s == SETU_BAD_SOCK) { fprintf(stderr, "connect failed (%d)\n", sock_errno()); net_cleanup(); return 1; }

    char req[1024];
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (send_all(s, req, (size_t)n) != 0) { fprintf(stderr, "send failed\n"); return 1; }

    char buf[4096]; long total = 0;
    for (;;) {                                    /* recv() returns whatever has arrived; 0 = peer's FIN */
        int k = recv(s, buf, sizeof buf, 0);
        if (k < 0) { fprintf(stderr, "recv error %d\n", sock_errno()); break; }
        if (k == 0) break;
        fwrite(buf, 1, (size_t)k, stdout);
        total += k;
    }
    fprintf(stderr, "\n[webget] %ld bytes received\n", total);
    sock_close(s);
    net_cleanup();
    return 0;
}
