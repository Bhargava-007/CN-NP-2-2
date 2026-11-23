/* server_models.c - CP2 on Windows: one request handler, three concurrency models.
     server_models.exe threads 8080     thread per connection (blocking I/O)
     server_models.exe select  8080     one thread, select()  - note Windows FD_SETSIZE
     server_models.exe poll    8080     one thread, WSAPoll() (poll() on Linux)
   The fourth model, IOCP (Windows' answer to epoll), is server_iocp.c.
   Every 5 s it prints: open connections, requests served, and the slowest loop iteration. */
#ifdef _WIN32
#define FD_SETSIZE 1024          /* Windows default is 64 sockets per fd_set; raise it, then measure the cost */
#endif
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
  #include <windows.h>
  #define poll_fn WSAPoll
  typedef WSAPOLLFD pollfd_t;
  static double now_ms(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return 1000.0 * c.QuadPart / f.QuadPart; }
#else
  #include <poll.h>
  #include <pthread.h>
  #define poll_fn poll
  typedef struct pollfd pollfd_t;
  static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
#endif

#define MAXC 20000
#define BUFSZ 4096
static const char RESP[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: keep-alive\r\n\r\nok\n";
static volatile long g_served = 0, g_open = 0;

typedef struct { sock_t fd; char buf[BUFSZ + 1]; size_t len; } conn;

/* The shared request handler: answers every complete request in the buffer (pipelining). */
static int handle_bytes(conn *c, const char *data, size_t n) {
    if (c->len + n > BUFSZ) c->len = 0;                 /* oversized: drop (starter simplification) */
    memcpy(c->buf + c->len, data, n); c->len += n;
    c->buf[c->len] = '\0';
    int answered = 0;
    char *e;
    while ((e = strstr(c->buf, "\r\n\r\n")) != NULL) {
        send(c->fd, RESP, (int)(sizeof RESP - 1), 0);   /* tiny response: assume it fits the send buffer */
        size_t used = (size_t)(e + 4 - c->buf);
        memmove(c->buf, c->buf + used, c->len - used + 1); c->len -= used;
        g_served++; answered++;
    }
    return answered;
}

static sock_t make_listener(int port) {
    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 1024)) { fprintf(stderr, "bind/listen failed %d\n", sock_errno()); exit(1); }
    return ls;
}

static void report(double *last, double worst) {
    double t = now_ms();
    if (t - *last >= 5000) { printf("open=%ld served=%ld slowest-loop=%.2f ms\n", g_open, g_served, worst); fflush(stdout); *last = t; }
}

/* ---------- model 1: thread per connection */
#ifdef _WIN32
static DWORD WINAPI worker(LPVOID p)
#else
static void *worker(void *p)
#endif
{
    conn *c = (conn *)p; char tmp[BUFSZ];
    for (;;) { int k = recv(c->fd, tmp, sizeof tmp, 0); if (k <= 0) break; handle_bytes(c, tmp, (size_t)k); }
    sock_close(c->fd); free(c); g_open--;
    return 0;
}
static void run_threads(sock_t ls) {
    for (;;) {
        sock_t s = accept(ls, NULL, NULL); if (s == SETU_BAD_SOCK) continue;
        conn *c = calloc(1, sizeof *c); c->fd = s; g_open++;
#ifdef _WIN32
        HANDLE h = CreateThread(NULL, 64 * 1024, worker, c, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL); if (h) CloseHandle(h);
#else
        pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 64 * 1024);
        pthread_create(&t, &at, worker, c); pthread_detach(t);
#endif
        if (g_open % 1000 == 0) printf("open=%ld (each thread reserves its own stack)\n", g_open);
    }
}

/* ---------- models 2 and 3: one thread, readiness scan */
static conn *cs[MAXC]; static int nc = 0;
static void accept_all(sock_t ls) {
    for (;;) {
        sock_t s = accept(ls, NULL, NULL);
        if (s == SETU_BAD_SOCK) return;                 /* non-blocking listener: drained */
        if (nc >= MAXC) { sock_close(s); continue; }
        sock_set_nonblocking(s);
        cs[nc] = calloc(1, sizeof(conn)); cs[nc]->fd = s; nc++; g_open++;
    }
}
static void read_ready(int i) {
    char tmp[BUFSZ];
    for (;;) {                                          /* drain until it would block */
        int k = recv(cs[i]->fd, tmp, sizeof tmp, 0);
        if (k > 0) { handle_bytes(cs[i], tmp, (size_t)k); continue; }
        if (k < 0 && SETU_WOULDBLOCK(sock_errno())) return;
        sock_close(cs[i]->fd); free(cs[i]); cs[i] = cs[--nc]; g_open--;   /* closed or error */
        return;
    }
}
static void run_select(sock_t ls) {
    double last = now_ms(), worst = 0;
    for (;;) {
        fd_set rs; FD_ZERO(&rs); FD_SET(ls, &rs);
        int maxfd = (int)ls, usable = nc < FD_SETSIZE - 1 ? nc : FD_SETSIZE - 1;
        for (int i = 0; i < usable; i++) { FD_SET(cs[i]->fd, &rs); if ((int)cs[i]->fd > maxfd) maxfd = (int)cs[i]->fd; }
        struct timeval tv = {1, 0};
        int r = select(maxfd + 1, &rs, NULL, NULL, &tv);   /* kernel scans the WHOLE set every call: O(N) */
        double t0 = now_ms();
        if (r > 0) {
            if (FD_ISSET(ls, &rs)) accept_all(ls);
            for (int i = usable - 1; i >= 0; i--) if (i < nc && FD_ISSET(cs[i]->fd, &rs)) read_ready(i);
        }
        double d = now_ms() - t0; if (d > worst) worst = d;
        if (nc >= FD_SETSIZE - 1) { static int warned; if (!warned++) printf("FD_SETSIZE (%d) reached: extra sockets are never watched\n", FD_SETSIZE); }
        report(&last, worst);
    }
}
static void run_poll(sock_t ls) {
    static pollfd_t p[MAXC + 1];
    double last = now_ms(), worst = 0;
    for (;;) {
        p[0].fd = ls; p[0].events = POLLIN; p[0].revents = 0;
        for (int i = 0; i < nc; i++) { p[i + 1].fd = cs[i]->fd; p[i + 1].events = POLLIN; p[i + 1].revents = 0; }
        int r = poll_fn(p, (unsigned long)(nc + 1), 1000); /* still hands the kernel the whole list each call */
        double t0 = now_ms();
        if (r > 0) {
            int n0 = nc;
            for (int i = n0 - 1; i >= 0; i--) if (p[i + 1].revents & (POLLIN | POLLERR | POLLHUP)) read_ready(i);
            if (p[0].revents & POLLIN) accept_all(ls);
        }
        double d = now_ms() - t0; if (d > worst) worst = d;
        report(&last, worst);
    }
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "poll"; int port = argc > 2 ? atoi(argv[2]) : 8080;
    if (net_init()) return 1;
    sock_t ls = make_listener(port);
    printf("model=%s port=%d\n", mode, port);
    if (!strcmp(mode, "threads")) run_threads(ls);
    sock_set_nonblocking(ls);
    if (!strcmp(mode, "select")) run_select(ls); else run_poll(ls);
    return 0;
}
