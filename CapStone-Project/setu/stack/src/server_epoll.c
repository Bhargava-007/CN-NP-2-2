/* server_epoll.c - CP2 model 4 (Linux, runs in the lab engine): one thread, epoll, edge-triggered.
   Reference implementation.  Build and run inside the engine:
       gcc -std=c17 -O2 -Wall -Wextra -Werror -o server_epoll src/server_epoll.c
       ./server_epoll et 8084        edge-triggered (EPOLLET): must drain to EAGAIN
       ./server_epoll lt 8084        level-triggered: one read per event is enough
   Every 5 s it prints: open connections, requests served, slowest loop iteration and the
   listen queue it last saw empty.                                                          */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAXEV 1024
#define BUFSZ 4096
static const char RESP[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: keep-alive\r\n\r\nok\n";
typedef struct { int fd; size_t len; char buf[BUFSZ + 1]; } conn;
static long g_open, g_served;

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int nonblock(int fd) { int f = fcntl(fd, F_GETFL, 0); return fcntl(fd, F_SETFL, f | O_NONBLOCK); }

static void handle_bytes(conn *c, const char *d, size_t n) {   /* answers every complete request (pipelining) */
    if (c->len + n > BUFSZ) c->len = 0;
    memcpy(c->buf + c->len, d, n); c->len += n; c->buf[c->len] = '\0';
    char *e;
    while ((e = strstr(c->buf, "\r\n\r\n")) != NULL) {
        if (send(c->fd, RESP, sizeof RESP - 1, MSG_NOSIGNAL) < 0) return;
        size_t used = (size_t)(e + 4 - c->buf);
        memmove(c->buf, c->buf + used, c->len - used + 1); c->len -= used; g_served++;
    }
}
static void drop(int ep, conn *c) { epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL); close(c->fd); free(c); g_open--; }

int main(int argc, char **argv) {
    int et = !(argc > 1 && !strcmp(argv[1], "lt")), port = argc > 2 ? atoi(argv[2]) : 8084;
    int ls = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 4096)) { perror("bind/listen"); return 1; }
    nonblock(ls);
    int ep = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = NULL };      /* NULL marks the listener */
    epoll_ctl(ep, EPOLL_CTL_ADD, ls, &ev);
    printf("model=epoll-%s port=%d\n", et ? "ET" : "LT", port); fflush(stdout);
    struct epoll_event evs[MAXEV];
    double last = now_ms(), worst = 0;
    for (;;) {
        int n = epoll_wait(ep, evs, MAXEV, 1000);          /* O(ready), not O(watched): the whole point */
        double t0 = now_ms();
        for (int i = 0; i < n; i++) {
            conn *c = evs[i].data.ptr;
            if (!c) {                                       /* listener: accept until EAGAIN */
                for (;;) {
                    int s = accept4(ls, NULL, NULL, SOCK_NONBLOCK);
                    if (s < 0) break;
                    conn *k = calloc(1, sizeof *k); k->fd = s; g_open++;
                    struct epoll_event e2 = { .events = EPOLLIN | EPOLLRDHUP | (et ? EPOLLET : 0), .data.ptr = k };
                    epoll_ctl(ep, EPOLL_CTL_ADD, s, &e2);
                }
                continue;
            }
            char tmp[BUFSZ];
            for (;;) {
                ssize_t k = recv(c->fd, tmp, sizeof tmp, 0);
                if (k > 0) { handle_bytes(c, tmp, (size_t)k); if (!et) break; continue; }   /* LT: one read is enough */
                if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;               /* ET: drained */
                if (k < 0 && errno == EINTR) continue;
                drop(ep, c); break;                                                          /* FIN or error */
            }
        }
        double d = now_ms() - t0; if (d > worst) worst = d;
        if (now_ms() - last >= 5000) { printf("open=%ld served=%ld slowest-loop=%.2f ms\n", g_open, g_served, worst); fflush(stdout); last = now_ms(); }
    }
}
