/* server_models.c - CP2 concurrency models (Linux only): threads, select, poll, epoll.
 * Purpose: compare thread-per-connection, select, poll, and level-triggered epoll
 *          under a shared pipelined HTTP/1.1 handler with bounded output queues.
 * Usage:   ./server_models <threads|select|poll|epoll> <port> [idle_s=30] [backlog=4096] [outcap=65536]
 * Build:   gcc -std=c17 -O2 -Wall -Wextra -Werror -pthread -o server_models stack/src/server_models.c
 * Status:  written, build not run.
 */
#ifdef _WIN32
#error "Linux only for now"
#endif

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define INBUF_SZ     4096
#define MAX_SAMPLES  4096

static const char RESP_200[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: keep-alive\r\n\r\nok\n";
static const char RESP_431[] =
    "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

typedef struct {
    int fd;
    char inbuf[INBUF_SZ + 1];
    size_t inlen;
    char *outbuf;
    size_t outcap;
    size_t outhead;
    size_t outlen;
    uint64_t last_active_ms;
    int closing;
    int peer_closed;
    int backpressured;
    uint32_t ep_events;
} conn_t;

static uint64_t g_open = 0;
static uint64_t g_served = 0;
static uint64_t g_idle_closed = 0;
static uint64_t g_backpressured = 0;
static uint64_t g_refused_fdsetsize = 0;
static int g_refused_printed = 0;

static double g_samples[MAX_SAMPLES];
static size_t g_nsamples = 0;

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ULL + (uint64_t)t.tv_nsec / 1000000ULL;
}

static double now_ms_f(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void record_sample(double ms) {
    if (g_nsamples < MAX_SAMPLES) {
        g_samples[g_nsamples++] = ms;
    }
}

static void maybe_report(uint64_t *last_rep_ms) {
    uint64_t t = now_ms();
    if (t - *last_rep_ms < 5000ULL) {
        return;
    }
    double p50 = 0.0, p99 = 0.0, mx = 0.0;
    if (g_nsamples > 0) {
        qsort(g_samples, g_nsamples, sizeof(double), cmp_double);
        p50 = g_samples[(g_nsamples - 1) * 50 / 100];
        p99 = g_samples[(g_nsamples - 1) * 99 / 100];
        mx = g_samples[g_nsamples - 1];
        g_nsamples = 0;
    }
    printf("open=%llu served=%llu idle_closed=%llu backpressured=%llu refused=%llu loop_ms(p50/p99/max)=%.3f/%.3f/%.3f\n",
           (unsigned long long)__atomic_load_n(&g_open, __ATOMIC_RELAXED),
           (unsigned long long)__atomic_load_n(&g_served, __ATOMIC_RELAXED),
           (unsigned long long)__atomic_load_n(&g_idle_closed, __ATOMIC_RELAXED),
           (unsigned long long)__atomic_load_n(&g_backpressured, __ATOMIC_RELAXED),
           (unsigned long long)__atomic_load_n(&g_refused_fdsetsize, __ATOMIC_RELAXED),
           p50, p99, mx);
    fflush(stdout);
    *last_rep_ms = t;
}

static int set_nonblocking(int fd) {
    int f = fcntl(fd, F_GETFL, 0);
    if (f < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

static conn_t *conn_new(int fd, size_t outcap) {
    conn_t *c = calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    c->outbuf = malloc(outcap);
    if (!c->outbuf) {
        free(c);
        return NULL;
    }
    c->fd = fd;
    c->outcap = outcap;
    c->last_active_ms = now_ms();
    __atomic_fetch_add(&g_open, 1, __ATOMIC_RELAXED);
    return c;
}

static void conn_free(conn_t *c) {
    if (!c) {
        return;
    }
    close(c->fd);
    free(c->outbuf);
    free(c);
    __atomic_fetch_sub(&g_open, 1, __ATOMIC_RELAXED);
}

/* Why: ring-buffer output queue avoids memmove on partial send() and bounds memory at outcap. */
static int outq_push(conn_t *c, const char *data, size_t len) {
    if (c->outlen + len > c->outcap) {
        return -1;
    }
    size_t tail = (c->outhead + c->outlen) % c->outcap;
    size_t first = c->outcap - tail;
    if (first > len) {
        first = len;
    }
    memcpy(c->outbuf + tail, data, first);
    if (len > first) {
        memcpy(c->outbuf, data + first, len - first);
    }
    c->outlen += len;
    return 0;
}

/* Why: flush queued bytes with MSG_NOSIGNAL so peer FIN/RST never raises SIGPIPE. */
static int outq_flush(conn_t *c) {
    while (c->outlen > 0) {
        size_t chunk = c->outcap - c->outhead;
        if (chunk > c->outlen) {
            chunk = c->outlen;
        }
        ssize_t n = send(c->fd, c->outbuf + c->outhead, chunk, MSG_NOSIGNAL);
        if (n > 0) {
            c->outhead = (c->outhead + (size_t)n) % c->outcap;
            c->outlen -= (size_t)n;
            c->last_active_ms = now_ms();
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    if (c->backpressured && c->outlen < c->outcap) {
        c->backpressured = 0;
    }
    return 0;
}

/* Why: parse pipelined \r\n\r\n requests; return -1 on fatal/431, 0 ok. */
static int process_input(conn_t *c) {
    char *e;
    while ((e = strstr(c->inbuf, "\r\n\r\n")) != NULL) {
        if (outq_push(c, RESP_200, sizeof(RESP_200) - 1) != 0) {
            if (!c->backpressured) {
                c->backpressured = 1;
                __atomic_fetch_add(&g_backpressured, 1, __ATOMIC_RELAXED);
            }
            return 0;
        }
        size_t used = (size_t)(e + 4 - c->inbuf);
        memmove(c->inbuf, c->inbuf + used, c->inlen - used + 1);
        c->inlen -= used;
        __atomic_fetch_add(&g_served, 1, __ATOMIC_RELAXED);
    }
    if (c->inlen >= INBUF_SZ) {
        /* Input buffer filled without a complete request: send 431 and close. */
        outq_push(c, RESP_431, sizeof(RESP_431) - 1);
        c->closing = 1;
    }
    return 0;
}

static int make_listener(int port, int backlog) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) {
        perror("socket");
        exit(1);
    }
    int on = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(ls, backlog) < 0) {
        perror("bind/listen");
        exit(1);
    }
    return ls;
}

/* ---------- model 1: thread per connection ---------- */
typedef struct {
    int fd;
    int idle_s;
    size_t outcap;
} thread_arg_t;

static void *worker_thread(void *p) {
    thread_arg_t *arg = (thread_arg_t *)p;
    int fd = arg->fd;
    int idle_s = arg->idle_s;
    size_t outcap = arg->outcap;
    free(arg);

    struct timeval tv = { .tv_sec = idle_s, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    conn_t *c = conn_new(fd, outcap);
    if (!c) {
        close(fd);
        return NULL;
    }

    for (;;) {
        if (c->inlen >= INBUF_SZ) {
            process_input(c);
            outq_flush(c);
            break;
        }
        ssize_t k = recv(c->fd, c->inbuf + c->inlen, INBUF_SZ - c->inlen, 0);
        if (k > 0) {
            c->inlen += (size_t)k;
            c->inbuf[c->inlen] = '\0';
            process_input(c);
            if (outq_flush(c) != 0 || c->closing) {
                break;
            }
        } else {
            if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                __atomic_fetch_add(&g_idle_closed, 1, __ATOMIC_RELAXED);
            }
            break;
        }
    }
    conn_free(c);
    return NULL;
}

static void run_threads(int ls, int idle_s, size_t outcap) {
    uint64_t last_rep = now_ms();
    for (;;) {
        int s = accept(ls, NULL, NULL);
        if (s < 0) {
            continue;
        }
        thread_arg_t *arg = malloc(sizeof(*arg));
        if (!arg) {
            close(s);
            continue;
        }
        arg->fd = s;
        arg->idle_s = idle_s;
        arg->outcap = outcap;

        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, 64 * 1024);
        if (pthread_create(&t, &at, worker_thread, arg) == 0) {
            pthread_detach(t);
        } else {
            free(arg);
            close(s);
        }
        pthread_attr_destroy(&at);
        maybe_report(&last_rep);
    }
}

/* ---------- shared non-blocking read/write for select/poll/epoll ---------- */
static int handle_readable(conn_t *c) {
    if (c->backpressured || c->closing || c->peer_closed) {
        return 0;
    }
    for (;;) {
        if (c->inlen >= INBUF_SZ) {
            process_input(c);
            return 0;
        }
        ssize_t k = recv(c->fd, c->inbuf + c->inlen, INBUF_SZ - c->inlen, 0);
        if (k > 0) {
            c->inlen += (size_t)k;
            c->inbuf[c->inlen] = '\0';
            c->last_active_ms = now_ms();
            process_input(c);
            if (c->backpressured || c->closing) {
                return 0;
            }
            continue;
        }
        if (k == 0) {
            c->peer_closed = 1;
            return 0;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
}

/* ---------- model 2: select ---------- */
static void run_select(int ls, int idle_s, size_t outcap) {
    set_nonblocking(ls);
    conn_t **cs = calloc(FD_SETSIZE, sizeof(conn_t *));
    int nc = 0;
    uint64_t last_rep = now_ms();
    uint64_t last_sweep = last_rep;

    for (;;) {
        fd_set rs, ws;
        FD_ZERO(&rs);
        FD_ZERO(&ws);
        FD_SET(ls, &rs);
        int maxfd = ls;
        for (int i = 0; i < nc; i++) {
            conn_t *c = cs[i];
            if (!c->backpressured && !c->closing && !c->peer_closed) {
                FD_SET(c->fd, &rs);
            }
            if (c->outlen > 0) {
                FD_SET(c->fd, &ws);
            }
            if (c->fd > maxfd) {
                maxfd = c->fd;
            }
        }
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int r = select(maxfd + 1, &rs, &ws, NULL, &tv);
        double t0 = now_ms_f();

        if (r > 0) {
            if (FD_ISSET(ls, &rs)) {
                for (;;) {
                    int s = accept(ls, NULL, NULL);
                    if (s < 0) {
                        break;
                    }
                    if (s >= FD_SETSIZE || nc >= FD_SETSIZE - 1) {
                        close(s);
                        __atomic_fetch_add(&g_refused_fdsetsize, 1, __ATOMIC_RELAXED);
                        if (!g_refused_printed) {
                            fprintf(stderr, "select: refused fd >= FD_SETSIZE (%d)\n", FD_SETSIZE);
                            g_refused_printed = 1;
                        }
                        continue;
                    }
                    set_nonblocking(s);
                    conn_t *c = conn_new(s, outcap);
                    if (!c) {
                        close(s);
                        continue;
                    }
                    cs[nc++] = c;
                }
            }
            for (int i = 0; i < nc; ) {
                conn_t *c = cs[i];
                int drop = 0;
                if (FD_ISSET(c->fd, &rs)) {
                    if (handle_readable(c) != 0) {
                        drop = 1;
                    } else if (c->outlen > 0) {
                        if (outq_flush(c) != 0) {
                            drop = 1;
                        }
                    }
                }
                if (!drop && FD_ISSET(c->fd, &ws)) {
                    if (outq_flush(c) != 0) {
                        drop = 1;
                    }
                }
                if (!drop && (c->closing || c->peer_closed) && c->outlen == 0) {
                    drop = 1;
                }
                if (drop) {
                    conn_free(c);
                    cs[i] = cs[--nc];
                } else {
                    i++;
                }
            }
        }

        uint64_t now = now_ms();
        if (now - last_sweep >= 1000ULL) {
            for (int i = 0; i < nc; ) {
                if (now - cs[i]->last_active_ms > (uint64_t)idle_s * 1000ULL) {
                    conn_free(cs[i]);
                    cs[i] = cs[--nc];
                    __atomic_fetch_add(&g_idle_closed, 1, __ATOMIC_RELAXED);
                } else {
                    i++;
                }
            }
            last_sweep = now;
        }
        record_sample(now_ms_f() - t0);
        maybe_report(&last_rep);
    }
}

/* ---------- model 3: poll ---------- */
static void run_poll(int ls, int idle_s, size_t outcap) {
    set_nonblocking(ls);
    size_t cap = 1024;
    conn_t **cs = calloc(cap, sizeof(conn_t *));
    struct pollfd *pfds = calloc(cap + 1, sizeof(struct pollfd));
    size_t nc = 0;
    uint64_t last_rep = now_ms();
    uint64_t last_sweep = last_rep;

    for (;;) {
        pfds[0].fd = ls;
        pfds[0].events = POLLIN;
        for (size_t i = 0; i < nc; i++) {
            pfds[i + 1].fd = cs[i]->fd;
            short ev = 0;
            if (!cs[i]->backpressured && !cs[i]->closing && !cs[i]->peer_closed) {
                ev |= POLLIN;
            }
            if (cs[i]->outlen > 0) {
                ev |= POLLOUT;
            }
            pfds[i + 1].events = ev;
        }

        int r = poll(pfds, (nfds_t)(nc + 1), 1000);
        double t0 = now_ms_f();

        if (r > 0) {
            size_t n_watched = nc;
            if (pfds[0].revents & POLLIN) {
                for (;;) {
                    int s = accept(ls, NULL, NULL);
                    if (s < 0) {
                        break;
                    }
                    set_nonblocking(s);
                    if (nc == cap) {
                        size_t ncap = cap * 2;
                        conn_t **ncs = realloc(cs, ncap * sizeof(conn_t *));
                        struct pollfd *npfds = realloc(pfds, (ncap + 1) * sizeof(struct pollfd));
                        if (!ncs || !npfds) {
                            close(s);
                            break;
                        }
                        cs = ncs;
                        pfds = npfds;
                        cap = ncap;
                    }
                    conn_t *c = conn_new(s, outcap);
                    if (!c) {
                        close(s);
                        continue;
                    }
                    cs[nc++] = c;
                }
            }
            /* Why: iterate backwards so swap-remove never shifts an unvisited pfds[i+1].revents. */
            for (size_t i = n_watched; i-- > 0; ) {
                conn_t *c = cs[i];
                short rev = pfds[i + 1].revents;
                int drop = 0;
                if (rev & (POLLERR | POLLNVAL)) {
                    drop = 1;
                }
                if (!drop && (rev & POLLIN)) {
                    if (handle_readable(c) != 0 || (c->outlen > 0 && outq_flush(c) != 0)) {
                        drop = 1;
                    }
                }
                if (!drop && (rev & POLLOUT)) {
                    if (outq_flush(c) != 0) {
                        drop = 1;
                    }
                }
                if (!drop && (rev & POLLHUP)) {
                    c->peer_closed = 1;
                }
                if (!drop && (c->closing || c->peer_closed) && c->outlen == 0) {
                    drop = 1;
                }
                if (drop) {
                    conn_free(c);
                    cs[i] = cs[--nc];
                }
            }
        }

        uint64_t now = now_ms();
        if (now - last_sweep >= 1000ULL) {
            for (size_t i = 0; i < nc; ) {
                if (now - cs[i]->last_active_ms > (uint64_t)idle_s * 1000ULL) {
                    conn_free(cs[i]);
                    cs[i] = cs[--nc];
                    __atomic_fetch_add(&g_idle_closed, 1, __ATOMIC_RELAXED);
                } else {
                    i++;
                }
            }
            last_sweep = now;
        }
        record_sample(now_ms_f() - t0);
        maybe_report(&last_rep);
    }
}

/* ---------- model 4: epoll (level-triggered) ---------- */
static void epoll_update(int ep, conn_t *c) {
    uint32_t ev = EPOLLRDHUP;
    if (!c->backpressured && !c->closing && !c->peer_closed) {
        ev |= EPOLLIN;
    }
    if (c->outlen > 0) {
        ev |= EPOLLOUT;
    }
    if (ev != c->ep_events) {
        struct epoll_event e = { .events = ev, .data.ptr = c };
        epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &e);
        c->ep_events = ev;
    }
}

static void run_epoll(int ls, int idle_s, size_t outcap) {
    set_nonblocking(ls);
    int ep = epoll_create1(0);
    if (ep < 0) {
        perror("epoll_create1");
        exit(1);
    }
    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = NULL };
    epoll_ctl(ep, EPOLL_CTL_ADD, ls, &ev);

    size_t cap = 1024;
    conn_t **cs = calloc(cap, sizeof(conn_t *));
    size_t nc = 0;
    struct epoll_event evs[1024];
    uint64_t last_rep = now_ms();
    uint64_t last_sweep = last_rep;

    for (;;) {
        int n = epoll_wait(ep, evs, 1024, 1000);
        double t0 = now_ms_f();

        conn_t *freed[1024];
        size_t nfreed = 0;

        for (int i = 0; i < n; i++) {
            conn_t *c = evs[i].data.ptr;
            if (!c) {
                for (;;) {
                    int s = accept4(ls, NULL, NULL, SOCK_NONBLOCK);
                    if (s < 0) {
                        break;
                    }
                    if (nc == cap) {
                        size_t ncap = cap * 2;
                        conn_t **ncs = realloc(cs, ncap * sizeof(conn_t *));
                        if (!ncs) {
                            close(s);
                            break;
                        }
                        cs = ncs;
                        cap = ncap;
                    }
                    conn_t *k = conn_new(s, outcap);
                    if (!k) {
                        close(s);
                        continue;
                    }
                    cs[nc++] = k;
                    k->ep_events = EPOLLIN | EPOLLRDHUP;
                    struct epoll_event e2 = { .events = k->ep_events, .data.ptr = k };
                    epoll_ctl(ep, EPOLL_CTL_ADD, s, &e2);
                }
                continue;
            }

            /* Why: skip events for a conn already queued for free earlier in this batch. */
            int already_freed = 0;
            for (size_t f = 0; f < nfreed; f++) {
                if (freed[f] == c) {
                    already_freed = 1;
                    break;
                }
            }
            if (already_freed) {
                continue;
            }

            int drop = 0;
            if (evs[i].events & EPOLLERR) {
                drop = 1;
            }
            if (!drop && (evs[i].events & EPOLLIN)) {
                if (handle_readable(c) != 0 || (c->outlen > 0 && outq_flush(c) != 0)) {
                    drop = 1;
                }
            }
            if (!drop && (evs[i].events & EPOLLOUT)) {
                if (outq_flush(c) != 0) {
                    drop = 1;
                }
            }
            if (!drop && (evs[i].events & (EPOLLHUP | EPOLLRDHUP))) {
                c->peer_closed = 1;
            }
            if (!drop && (c->closing || c->peer_closed) && c->outlen == 0) {
                drop = 1;
            }
            if (drop) {
                epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
                for (size_t j = 0; j < nc; j++) {
                    if (cs[j] == c) {
                        cs[j] = cs[--nc];
                        break;
                    }
                }
                freed[nfreed++] = c;
            } else {
                epoll_update(ep, c);
            }
        }
        for (size_t f = 0; f < nfreed; f++) {
            conn_free(freed[f]);
        }

        uint64_t now = now_ms();
        if (now - last_sweep >= 1000ULL) {
            for (size_t i = 0; i < nc; ) {
                if (now - cs[i]->last_active_ms > (uint64_t)idle_s * 1000ULL) {
                    epoll_ctl(ep, EPOLL_CTL_DEL, cs[i]->fd, NULL);
                    conn_free(cs[i]);
                    cs[i] = cs[--nc];
                    __atomic_fetch_add(&g_idle_closed, 1, __ATOMIC_RELAXED);
                } else {
                    i++;
                }
            }
            last_sweep = now;
        }
        record_sample(now_ms_f() - t0);
        maybe_report(&last_rep);
    }
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 6) {
        fprintf(stderr,
                "Usage: %s <threads|select|poll|epoll> <port> [idle_s=30] [backlog=4096] [outcap=65536]\n",
                argc > 0 ? argv[0] : "server_models");
        return 2;
    }

    const char *mode = argv[1];
    int port = atoi(argv[2]);
    int idle_s = (argc >= 4) ? atoi(argv[3]) : 30;
    int backlog = (argc >= 5) ? atoi(argv[4]) : 4096;
    size_t outcap = (argc >= 6) ? (size_t)strtoull(argv[5], NULL, 10) : 65536u;

    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &rl) == 0) {
            getrlimit(RLIMIT_NOFILE, &rl);
        }
        printf("RLIMIT_NOFILE cur=%llu max=%llu\n",
               (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max);
    }

    int ls = make_listener(port, backlog);
    printf("model=%s port=%d idle_s=%d backlog=%d outcap=%zu\n",
           mode, port, idle_s, backlog, outcap);
    fflush(stdout);

    if (strcmp(mode, "threads") == 0) {
        run_threads(ls, idle_s, outcap);
    } else if (strcmp(mode, "select") == 0) {
        run_select(ls, idle_s, outcap);
    } else if (strcmp(mode, "poll") == 0) {
        run_poll(ls, idle_s, outcap);
    } else if (strcmp(mode, "epoll") == 0) {
        run_epoll(ls, idle_s, outcap);
    } else {
        fprintf(stderr, "Unknown mode '%s'\n", mode);
        return 2;
    }
    return 0;
}
