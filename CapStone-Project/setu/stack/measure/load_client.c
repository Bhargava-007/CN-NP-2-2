/* load_client.c - C17 epoll load client for CP2 concurrency cost curves (Linux only).
 * Purpose: measure latency (p50/p95/p99/max) and throughput under N idle + A active connections
 *          across K forked processes without hitting Python's single-core CPU ceiling.
 * Usage:   ./load_client --port P [--host 127.0.0.1] --idle N [--active 50] [--seconds 10] [--warmup 2] [--procs 1]
 * Build:   gcc -std=c17 -O2 -Wall -Wextra -Werror -o load_client load_client.c
 * Status:  written, build not run.
 */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_SAMPLES_PER_PROC 4000000u
#define INBUF_SZ 8192

static const char REQ[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
#define REQ_LEN (sizeof(REQ) - 1)

typedef enum {
    ST_CONNECTING = 0,
    ST_SENDING,
    ST_RECV
} conn_state_t;

typedef struct {
    int fd;
    conn_state_t state;
    size_t sent;
    char inbuf[INBUF_SZ];
    size_t inlen;
    uint64_t t_send_ns;
    uint32_t ep_events;
} active_conn_t;

typedef struct {
    uint64_t errors;
    size_t nsamples;
} child_hdr_t;

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int set_nonblocking(int fd) {
    int f = fcntl(fd, F_GETFL, 0);
    if (f < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, p + off, len - off);
        if (n == 0) {
            return -1;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static void *find_crlfcrlf(const char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return (void *)(buf + i);
        }
    }
    return NULL;
}

static int ci_starts_with(const char *s, size_t len, const char *prefix) {
    size_t plen = strlen(prefix);
    if (len < plen) {
        return 0;
    }
    for (size_t i = 0; i < plen; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
            return 0;
        }
    }
    return 1;
}

static size_t parse_content_length(const char *hdr, size_t hdr_len) {
    size_t pos = 0;
    while (pos < hdr_len) {
        size_t eol = pos;
        while (eol + 1 < hdr_len && !(hdr[eol] == '\r' && hdr[eol + 1] == '\n')) {
            eol++;
        }
        size_t llen = eol - pos;
        if (ci_starts_with(hdr + pos, llen, "content-length:")) {
            const char *v = hdr + pos + 15;
            const char *end = hdr + pos + llen;
            while (v < end && (*v == ' ' || *v == '\t')) {
                v++;
            }
            size_t cl = 0;
            while (v < end && *v >= '0' && *v <= '9') {
                cl = cl * 10u + (size_t)(*v - '0');
                v++;
            }
            return cl;
        }
        pos = eol + 2;
    }
    return 3;
}

static void epoll_set(int ep, active_conn_t *c, uint32_t ev) {
    if (ev != c->ep_events) {
        struct epoll_event e = { .events = ev, .data.ptr = c };
        epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &e);
        c->ep_events = ev;
    }
}

static void run_child(const struct sockaddr_storage *addr, socklen_t addrlen,
                      int count, double seconds, double warmup, int pipe_w) {
    uint32_t *samples = malloc((size_t)MAX_SAMPLES_PER_PROC * sizeof(uint32_t));
    if (!samples) {
        child_hdr_t hdr = { .errors = (uint64_t)count, .nsamples = 0 };
        write_all(pipe_w, &hdr, sizeof(hdr));
        close(pipe_w);
        _exit(1);
    }

    int ep = epoll_create1(0);
    if (ep < 0) {
        free(samples);
        child_hdr_t hdr = { .errors = (uint64_t)count, .nsamples = 0 };
        write_all(pipe_w, &hdr, sizeof(hdr));
        close(pipe_w);
        _exit(1);
    }

    active_conn_t *conns = calloc((size_t)count, sizeof(active_conn_t));
    uint64_t errors = 0;
    size_t nsamples = 0;

    for (int i = 0; i < count; i++) {
        int s = socket(addr->ss_family, SOCK_STREAM, 0);
        if (s < 0) {
            conns[i].fd = -1;
            errors++;
            continue;
        }
        set_nonblocking(s);
        int rc = connect(s, (const struct sockaddr *)addr, addrlen);
        if (rc < 0 && errno != EINPROGRESS) {
            close(s);
            conns[i].fd = -1;
            errors++;
            continue;
        }
        conns[i].fd = s;
        conns[i].state = (rc == 0) ? ST_SENDING : ST_CONNECTING;
        conns[i].ep_events = EPOLLOUT | EPOLLRDHUP;
        struct epoll_event e = { .events = conns[i].ep_events, .data.ptr = &conns[i] };
        epoll_ctl(ep, EPOLL_CTL_ADD, s, &e);
    }

    uint64_t t_start = now_ns();
    uint64_t t_warmup_end = t_start + (uint64_t)(warmup * 1e9);
    uint64_t t_end = t_start + (uint64_t)(seconds * 1e9);
    struct epoll_event evs[1024];

    for (;;) {
        uint64_t now = now_ns();
        if (now >= t_end) {
            break;
        }
        int timeout_ms = (int)((t_end - now) / 1000000ULL) + 1;
        if (timeout_ms > 200) {
            timeout_ms = 200;
        }
        int n = epoll_wait(ep, evs, 1024, timeout_ms);
        for (int i = 0; i < n; i++) {
            active_conn_t *c = (active_conn_t *)evs[i].data.ptr;
            if (c->fd < 0) {
                continue;
            }
            if (evs[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
                close(c->fd);
                c->fd = -1;
                errors++;
                continue;
            }
            if (c->state == ST_CONNECTING && (evs[i].events & EPOLLOUT)) {
                int err = 0;
                socklen_t elen = sizeof(err);
                if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
                    epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
                    close(c->fd);
                    c->fd = -1;
                    errors++;
                    continue;
                }
                c->state = ST_SENDING;
                c->sent = 0;
            }
            if (c->state == ST_SENDING && (evs[i].events & EPOLLOUT)) {
                if (c->sent == 0) {
                    c->t_send_ns = now_ns();
                }
                while (c->sent < REQ_LEN) {
                    ssize_t k = send(c->fd, REQ + c->sent, REQ_LEN - c->sent, MSG_NOSIGNAL);
                    if (k > 0) {
                        c->sent += (size_t)k;
                    } else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else if (k < 0 && errno == EINTR) {
                        continue;
                    } else {
                        epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
                        close(c->fd);
                        c->fd = -1;
                        errors++;
                        break;
                    }
                }
                if (c->fd < 0) {
                    continue;
                }
                if (c->sent == REQ_LEN) {
                    c->state = ST_RECV;
                    epoll_set(ep, c, EPOLLIN | EPOLLRDHUP);
                }
            }
            if (c->state == ST_RECV && (evs[i].events & EPOLLIN)) {
                int closed = 0;
                for (;;) {
                    if (c->inlen >= INBUF_SZ) {
                        closed = 1;
                        break;
                    }
                    ssize_t k = recv(c->fd, c->inbuf + c->inlen, INBUF_SZ - c->inlen, 0);
                    if (k > 0) {
                        c->inlen += (size_t)k;
                    } else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else if (k < 0 && errno == EINTR) {
                        continue;
                    } else {
                        closed = 1;
                        break;
                    }
                }
                if (closed) {
                    epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
                    close(c->fd);
                    c->fd = -1;
                    errors++;
                    continue;
                }
                char *sep = (char *)find_crlfcrlf(c->inbuf, c->inlen);
                if (sep) {
                    size_t hdr_end = (size_t)(sep - c->inbuf) + 4;
                    size_t cl = parse_content_length(c->inbuf, (size_t)(sep - c->inbuf));
                    size_t total = hdr_end + cl;
                    if (c->inlen >= total) {
                        uint64_t t_done = now_ns();
                        if (c->t_send_ns >= t_warmup_end && nsamples < MAX_SAMPLES_PER_PROC) {
                            uint64_t us = (t_done - c->t_send_ns) / 1000ULL;
                            samples[nsamples++] = (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
                        }
                        if (c->inlen > total) {
                            memmove(c->inbuf, c->inbuf + total, c->inlen - total);
                        }
                        c->inlen -= total;
                        c->state = ST_SENDING;
                        c->sent = 0;
                        epoll_set(ep, c, EPOLLOUT | EPOLLRDHUP);
                    }
                }
            }
        }
    }

    for (int i = 0; i < count; i++) {
        if (conns[i].fd >= 0) {
            close(conns[i].fd);
        }
    }
    close(ep);
    free(conns);

    child_hdr_t hdr = { .errors = errors, .nsamples = nsamples };
    write_all(pipe_w, &hdr, sizeof(hdr));
    if (nsamples > 0) {
        write_all(pipe_w, samples, nsamples * sizeof(uint32_t));
    }
    close(pipe_w);
    free(samples);
    _exit(0);
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t ua = *(const uint32_t *)a;
    uint32_t ub = *(const uint32_t *)b;
    return (ua > ub) - (ua < ub);
}

static double percentile_ms(const uint32_t *sorted, size_t n, double p) {
    if (n == 0) {
        return 0.0;
    }
    size_t rank = (size_t)ceil((p / 100.0) * (double)n);
    if (rank < 1) {
        rank = 1;
    }
    if (rank > n) {
        rank = n;
    }
    return (double)sorted[rank - 1] / 1000.0;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    const char *port_str = NULL;
    int idle_n = -1;
    int active_n = 50;
    double seconds = 10.0;
    double warmup = 2.0;
    int procs = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port_str = argv[++i];
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--idle") == 0 && i + 1 < argc) {
            idle_n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--active") == 0 && i + 1 < argc) {
            active_n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup = atof(argv[++i]);
        } else if (strcmp(argv[i], "--procs") == 0 && i + 1 < argc) {
            procs = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown or incomplete arg: %s\n", argv[i]);
            return 2;
        }
    }

    if (!port_str || idle_n < 0 || active_n < 0 || procs < 1) {
        fprintf(stderr,
                "Usage: %s --port P [--host 127.0.0.1] --idle N [--active 50] [--seconds 10] [--warmup 2] [--procs 1]\n",
                argc > 0 ? argv[0] : "load_client");
        return 2;
    }

    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        fprintf(stderr, "getaddrinfo failed for %s:%s\n", host, port_str);
        return 1;
    }
    struct sockaddr_storage addr;
    socklen_t addrlen = res->ai_addrlen;
    memcpy(&addr, res->ai_addr, addrlen);
    freeaddrinfo(res);

    /* Open N idle connections in the parent, ramped 500 per 50 ms. */
    int *idle_fds = calloc((size_t)(idle_n > 0 ? idle_n : 1), sizeof(int));
    int idle_opened = 0;
    uint64_t total_errors = 0;
    for (int i = 0; i < idle_n; i++) {
        int s = socket(addr.ss_family, SOCK_STREAM, 0);
        if (s >= 0 && connect(s, (const struct sockaddr *)&addr, addrlen) == 0) {
            idle_fds[idle_opened++] = s;
        } else {
            if (s >= 0) {
                close(s);
            }
            total_errors++;
        }
        if ((i + 1) % 500 == 0 && i + 1 < idle_n) {
            sleep_ms(50);
        }
    }
    printf("idle_opened=%d/%d\n", idle_opened, idle_n);
    fflush(stdout);

    int *pipes_r = calloc((size_t)procs, sizeof(int));
    pid_t *pids = calloc((size_t)procs, sizeof(pid_t));
    int base = active_n / procs;
    int rem = active_n % procs;

    for (int p = 0; p < procs; p++) {
        int pfd[2];
        if (pipe(pfd) != 0) {
            perror("pipe");
            return 1;
        }
        int cnt = base + (p < rem ? 1 : 0);
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            close(pfd[0]);
            run_child(&addr, addrlen, cnt, seconds, warmup, pfd[1]);
        }
        close(pfd[1]);
        pipes_r[p] = pfd[0];
        pids[p] = pid;
    }

    size_t total_samples = 0;
    size_t cap_samples = 0;
    uint32_t *all_samples = NULL;

    for (int p = 0; p < procs; p++) {
        child_hdr_t hdr = {0};
        if (read_all(pipes_r[p], &hdr, sizeof(hdr)) == 0) {
            total_errors += hdr.errors;
            if (hdr.nsamples > 0) {
                if (total_samples + hdr.nsamples > cap_samples) {
                    cap_samples = total_samples + hdr.nsamples;
                    all_samples = realloc(all_samples, cap_samples * sizeof(uint32_t));
                }
                if (all_samples) {
                    if (read_all(pipes_r[p], all_samples + total_samples,
                                 hdr.nsamples * sizeof(uint32_t)) == 0) {
                        total_samples += hdr.nsamples;
                    }
                }
            }
        }
        close(pipes_r[p]);
        waitpid(pids[p], NULL, 0);
    }

    for (int i = 0; i < idle_opened; i++) {
        close(idle_fds[i]);
    }
    free(idle_fds);
    free(pipes_r);
    free(pids);

    double measure_s = seconds - warmup;
    if (measure_s < 0.001) {
        measure_s = 0.001;
    }
    if (total_samples > 0) {
        qsort(all_samples, total_samples, sizeof(uint32_t), cmp_u32);
    }
    double rps = (double)total_samples / measure_s;
    double p50 = percentile_ms(all_samples, total_samples, 50.0);
    double p95 = percentile_ms(all_samples, total_samples, 95.0);
    double p99 = percentile_ms(all_samples, total_samples, 99.0);
    double mx = (total_samples > 0) ? (double)all_samples[total_samples - 1] / 1000.0 : 0.0;

    printf("idle_opened=%d active=%d requests=%zu rps=%.1f p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
           idle_opened, active_n, total_samples, rps, p50, p95, p99, mx);
    if (total_errors > 0) {
        printf("ERRORS=%llu\n", (unsigned long long)total_errors);
    }
    free(all_samples);
    return 0;
}
