/* http_server.c - CP1a starter: an HTTP/1.1 server that a real browser renders.
   Single connection at a time (week 5 adds concurrency). Run:  http_server.exe 8080
   then open http://localhost:8080/ in Edge or Chrome.
   What is implemented: accumulate bytes across recv() calls, find the end of headers once,
   require Host, reject Content-Length + Transfer-Encoding together (400), read a CL body,
   send_all(), FIN handling. TODO for CP1: chunked request bodies, keep-alive, pipelining. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_REQ 65536

static int send_all(sock_t s, const char *p, size_t n) {
    while (n) { int k = send(s, p, (int)n, 0); if (k <= 0) return -1; p += k; n -= (size_t)k; }
    return 0;
}

static const char *find_header(const char *hdrs, const char *name) {
    size_t nl = strlen(name);
    for (const char *p = hdrs; (p = strstr(p, "\r\n")) != NULL; ) {
        p += 2;
        if (strncmp(p, "\r\n", 2) == 0) return NULL;          /* blank line: end of headers */
        int match = 1;
        for (size_t i = 0; i < nl; i++) if (tolower((unsigned char)p[i]) != tolower((unsigned char)name[i])) { match = 0; break; }
        if (match && p[nl] == ':') { p += nl + 1; while (*p == ' ') p++; return p; }
    }
    return NULL;
}

static void respond(sock_t c, int code, const char *reason, const char *body) {
    char hdr[256];
    int n = snprintf(hdr, sizeof hdr, "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %zu\r\nConnection: close\r\n\r\n", code, reason, strlen(body));
    if (send_all(c, hdr, (size_t)n) == 0) send_all(c, body, strlen(body));
}

static void serve(sock_t c) {
    char *req = malloc(MAX_REQ + 1);
    size_t have = 0; long hdr_end = -1;
    while (hdr_end < 0) {                                    /* accumulate: one recv() may hold 1 byte */
        if (have == MAX_REQ) { respond(c, 431, "Request Header Fields Too Large", "too large"); goto done; }
        int k = recv(c, req + have, (int)(MAX_REQ - have), 0);
        if (k <= 0) goto done;                               /* 0 = FIN before a full request */
        size_t from = have >= 3 ? have - 3 : 0;              /* search only the new bytes (+3 overlap) */
        have += (size_t)k; req[have] = '\0';
        char *e = strstr(req + from, "\r\n\r\n");
        if (e) hdr_end = (long)(e - req) + 4;
    }
    char saved = req[hdr_end]; req[hdr_end] = '\0';
    const char *host = find_header(req, "Host");
    const char *cl = find_header(req, "Content-Length");
    const char *te = find_header(req, "Transfer-Encoding");
    req[hdr_end] = saved;
    char method[8] = {0}, path[1024] = {0};
    if (sscanf(req, "%7s %1023s", method, path) != 2) { respond(c, 400, "Bad Request", "bad request line"); goto done; }
    if (!host) { respond(c, 400, "Bad Request", "HTTP/1.1 requires Host"); goto done; }
    if (cl && te) { respond(c, 400, "Bad Request", "Content-Length and Transfer-Encoding together are rejected"); goto done; }
    if (cl) {                                                /* read exactly Content-Length body bytes */
        long need = strtol(cl, NULL, 10) - (long)(have - (size_t)hdr_end);
        while (need > 0) { char tmp[4096]; int k = recv(c, tmp, (int)(need < 4096 ? need : 4096), 0); if (k <= 0) goto done; need -= k; }
    }
    char body[2048];
    snprintf(body, sizeof body, "<!doctype html><title>SETU</title><h1>SETU service</h1>"
             "<p>You asked for <code>%s %s</code>. Served by your own CP1 server.</p>", method, path);
    respond(c, 200, "OK", body);
done:
    shutdown(c, SETU_SHUT_WR);                               /* finish sending, then close */
    free(req);
    sock_close(c);
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (net_init() != 0) return 1;
    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0 || listen(ls, 128) != 0) { fprintf(stderr, "bind/listen failed %d\n", sock_errno()); return 1; }
    printf("listening on http://localhost:%d/  (Ctrl+C to stop)\n", port);
    for (;;) {
        sock_t c = accept(ls, NULL, NULL);                   /* takes one finished handshake off the accept queue */
        if (c == SETU_BAD_SOCK) continue;
        serve(c);
    }
}
