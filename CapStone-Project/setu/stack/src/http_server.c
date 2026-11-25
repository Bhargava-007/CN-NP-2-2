#include "platform.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HDR_BYTES   16384
#define MAX_BODY_BYTES  (1024u * 1024u)
#define MAX_REQS_CONN   100
#define IDLE_TIMEOUT_MS 5000
#define CONN_BUF_CAP    32768

typedef struct {
    sock_t fd;
    char buf[CONN_BUF_CAP];
    size_t len;
} conn_t;

/* Why: TCP send() may write fewer bytes than requested; loop until all bytes are sent. */
static int send_all(sock_t s, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = send(s, data + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(s, data + sent, len - sent, 0);
#endif
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int ci_eq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

static int ci_eq(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    return la == lb && ci_eq_n(a, b, la);
}

static void trim_ows(const char **p, size_t *len) {
    while (*len > 0 && (**p == ' ' || **p == '\t')) {
        (*p)++;
        (*len)--;
    }
    while (*len > 0 && ((*p)[*len - 1] == ' ' || (*p)[*len - 1] == '\t')) {
        (*len)--;
    }
}

static int parse_u64_digits(const char *s, size_t len, uint64_t *out) {
    if (len == 0) {
        return -1;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10u) {
            return -1;
        }
        v = v * 10u + d;
    }
    *out = v;
    return 0;
}

static int send_response(sock_t fd, int code, const char *reason, const char *ctype,
                         const char *extra_hdr, const char *body, size_t body_len,
                         int head_only, int keep_alive) {
    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: %s\r\n"
                     "%s\r\n",
                     code, reason, ctype, body_len,
                     keep_alive ? "keep-alive" : "close",
                     extra_hdr ? extra_hdr : "");
    if (n < 0 || (size_t)n >= sizeof(hdr)) {
        return -1;
    }
    if (send_all(fd, hdr, (size_t)n) != 0) {
        return -1;
    }
    if (!head_only && body_len > 0) {
        if (send_all(fd, body, body_len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int send_error(sock_t fd, int code, const char *reason, const char *extra_hdr,
                      const char *msg) {
    return send_response(fd, code, reason, "text/plain; charset=utf-8",
                         extra_hdr, msg, strlen(msg), 0, 0);
}

/* Why: pull more bytes into conn_t without overflowing CONN_BUF_CAP. */
static int conn_fill(conn_t *c) {
    if (c->len >= CONN_BUF_CAP) {
        return -2;
    }
#ifdef _WIN32
    int n = recv(c->fd, c->buf + c->len, (int)(CONN_BUF_CAP - c->len), 0);
#else
    ssize_t n = recv(c->fd, c->buf + c->len, CONN_BUF_CAP - c->len, 0);
#endif
    if (n <= 0) {
        return -1;
    }
    c->len += (size_t)n;
    return 0;
}

static void conn_consume(conn_t *c, size_t n) {
    if (n >= c->len) {
        c->len = 0;
    } else {
        memmove(c->buf, c->buf + n, c->len - n);
        c->len -= n;
    }
}

static int find_crlfcrlf(const char *b, size_t len, size_t *end_pos) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') {
            *end_pos = i + 4;
            return 1;
        }
    }
    return 0;
}

static int find_crlf(const char *b, size_t len, size_t *eol) {
    for (size_t i = 0; i + 1 < len; i++) {
        if (b[i] == '\r' && b[i + 1] == '\n') {
            *eol = i;
            return 1;
        }
    }
    return 0;
}

/* Why: discard fixed-length body without allocating unbounded memory. */
static int drain_fixed_body(conn_t *c, size_t rem) {
    while (rem > 0) {
        if (c->len == 0) {
            if (conn_fill(c) != 0) {
                return -1;
            }
        }
        size_t take = c->len < rem ? c->len : rem;
        conn_consume(c, take);
        rem -= take;
    }
    return 0;
}

/* Why: parse chunked framing incrementally; cap total body at MAX_BODY_BYTES. */
static int drain_chunked_body(conn_t *c, int *too_large) {
    size_t total = 0;
    *too_large = 0;
    for (;;) {
        size_t eol = 0;
        while (!find_crlf(c->buf, c->len, &eol)) {
            if (c->len >= MAX_HDR_BYTES) {
                return -1;
            }
            if (conn_fill(c) != 0) {
                return -1;
            }
        }
        size_t hex_len = eol;
        for (size_t i = 0; i < eol; i++) {
            if (c->buf[i] == ';') {
                hex_len = i;
                break;
            }
        }
        const char *hp = c->buf;
        trim_ows(&hp, &hex_len);
        if (hex_len == 0) {
            return -1;
        }
        size_t chunk_sz = 0;
        for (size_t i = 0; i < hex_len; i++) {
            char ch = hp[i];
            unsigned int d;
            if (ch >= '0' && ch <= '9') {
                d = (unsigned int)(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                d = 10u + (unsigned int)(ch - 'a');
            } else if (ch >= 'A' && ch <= 'F') {
                d = 10u + (unsigned int)(ch - 'A');
            } else {
                return -1;
            }
            if (chunk_sz > (MAX_BODY_BYTES - d) / 16u) {
                *too_large = 1;
                return -1;
            }
            chunk_sz = chunk_sz * 16u + d;
        }
        conn_consume(c, eol + 2);

        if (chunk_sz == 0) {
            /* Read and discard trailers until empty line. */
            for (;;) {
                while (!find_crlf(c->buf, c->len, &eol)) {
                    if (c->len >= MAX_HDR_BYTES) {
                        return -1;
                    }
                    if (conn_fill(c) != 0) {
                        return -1;
                    }
                }
                conn_consume(c, eol + 2);
                if (eol == 0) {
                    return 0;
                }
            }
        }

        if (total + chunk_sz > MAX_BODY_BYTES) {
            *too_large = 1;
            return -1;
        }
        total += chunk_sz;
        if (drain_fixed_body(c, chunk_sz) != 0) {
            return -1;
        }
        while (c->len < 2) {
            if (conn_fill(c) != 0) {
                return -1;
            }
        }
        if (c->buf[0] != '\r' || c->buf[1] != '\n') {
            return -1;
        }
        conn_consume(c, 2);
    }
}

static void handle_connection(sock_t fd) {
    conn_t c;
    c.fd = fd;
    c.len = 0;

    /* Why: bound idle time between pipelined/keep-alive requests to 5 s. */
#ifdef _WIN32
    DWORD tv = IDLE_TIMEOUT_MS;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = IDLE_TIMEOUT_MS / 1000;
    tv.tv_usec = (IDLE_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    for (int req_num = 0; req_num < MAX_REQS_CONN; req_num++) {
        size_t hdr_end = 0;
        while (!find_crlfcrlf(c.buf, c.len, &hdr_end)) {
            if (c.len > MAX_HDR_BYTES) {
                send_error(fd, 431, "Request Header Fields Too Large", NULL, "headers too large\n");
                return;
            }
            if (conn_fill(&c) != 0) {
                return;
            }
        }
        if (hdr_end > MAX_HDR_BYTES) {
            send_error(fd, 431, "Request Header Fields Too Large", NULL, "headers too large\n");
            return;
        }

        /* Parse request line. */
        size_t rl_end = 0;
        if (!find_crlf(c.buf, hdr_end, &rl_end) || rl_end == 0) {
            send_error(fd, 400, "Bad Request", NULL, "bad request line\n");
            return;
        }
        const char *rl = c.buf;
        const char *sp1 = memchr(rl, ' ', rl_end);
        if (!sp1 || sp1 == rl) {
            send_error(fd, 400, "Bad Request", NULL, "bad request line\n");
            return;
        }
        size_t method_len = (size_t)(sp1 - rl);
        const char *t_start = sp1 + 1;
        size_t rem_rl = rl_end - method_len - 1;
        const char *sp2 = memchr(t_start, ' ', rem_rl);
        if (!sp2 || sp2 == t_start) {
            send_error(fd, 400, "Bad Request", NULL, "bad request line\n");
            return;
        }
        size_t target_len = (size_t)(sp2 - t_start);
        const char *ver = sp2 + 1;
        size_t ver_len = rl_end - (size_t)(ver - rl);
        if (memchr(ver, ' ', ver_len) != NULL || ver_len != 8 || memcmp(ver, "HTTP/", 5) != 0) {
            send_error(fd, 400, "Bad Request", NULL, "bad version\n");
            return;
        }
        int is_11 = 0;
        if (memcmp(ver, "HTTP/1.1", 8) == 0) {
            is_11 = 1;
        } else if (memcmp(ver, "HTTP/1.0", 8) != 0) {
            send_error(fd, 505, "HTTP Version Not Supported", NULL, "unsupported version\n");
            return;
        }

        char method[16];
        if (method_len >= sizeof(method)) {
            send_error(fd, 405, "Method Not Allowed", "Allow: GET, HEAD\r\n", "method not allowed\n");
            return;
        }
        memcpy(method, rl, method_len);
        method[method_len] = '\0';

        char target[2048];
        if (target_len >= sizeof(target)) {
            send_error(fd, 400, "Bad Request", NULL, "target too long\n");
            return;
        }
        memcpy(target, t_start, target_len);
        target[target_len] = '\0';

        /* Parse headers. */
        int has_host = 0;
        int has_cl = 0;
        uint64_t cl_val = 0;
        int has_te = 0;
        int te_chunked = 0;
        int conn_close = 0;
        int conn_keep = 0;

        size_t pos = rl_end + 2;
        while (pos < hdr_end - 2) {
            size_t line_len = 0;
            if (!find_crlf(c.buf + pos, hdr_end - pos, &line_len)) {
                send_error(fd, 400, "Bad Request", NULL, "bad header\n");
                return;
            }
            const char *line = c.buf + pos;
            const char *colon = memchr(line, ':', line_len);
            if (!colon || colon == line) {
                send_error(fd, 400, "Bad Request", NULL, "malformed header\n");
                return;
            }
            size_t klen = (size_t)(colon - line);
            for (size_t i = 0; i < klen; i++) {
                if (line[i] == ' ' || line[i] == '\t') {
                    send_error(fd, 400, "Bad Request", NULL, "invalid header name\n");
                    return;
                }
            }
            const char *val = colon + 1;
            size_t vlen = line_len - klen - 1;
            trim_ows(&val, &vlen);

            if (klen == 4 && ci_eq_n(line, "Host", 4)) {
                has_host = 1;
            } else if (klen == 14 && ci_eq_n(line, "Content-Length", 14)) {
                uint64_t v = 0;
                if (parse_u64_digits(val, vlen, &v) != 0) {
                    send_error(fd, 400, "Bad Request", NULL, "invalid content-length\n");
                    return;
                }
                if (v > MAX_BODY_BYTES) {
                    send_error(fd, 413, "Content Too Large", NULL, "body too large\n");
                    return;
                }
                if (has_cl && v != cl_val) {
                    send_error(fd, 400, "Bad Request", NULL, "conflicting content-length\n");
                    return;
                }
                has_cl = 1;
                cl_val = v;
            } else if (klen == 17 && ci_eq_n(line, "Transfer-Encoding", 17)) {
                has_te = 1;
                if (vlen == 7 && ci_eq_n(val, "chunked", 7)) {
                    te_chunked = 1;
                } else {
                    send_error(fd, 501, "Not Implemented", NULL, "unsupported transfer-encoding\n");
                    return;
                }
            } else if (klen == 10 && ci_eq_n(line, "Connection", 10)) {
                if (vlen == 5 && ci_eq_n(val, "close", 5)) {
                    conn_close = 1;
                } else if (vlen == 10 && ci_eq_n(val, "keep-alive", 10)) {
                    conn_keep = 1;
                }
            }
            pos += line_len + 2;
        }

        if (is_11 && !has_host) {
            send_error(fd, 400, "Bad Request", NULL, "missing Host\n");
            return;
        }
        if (has_cl && has_te) {
            send_error(fd, 400, "Bad Request", NULL, "CL and TE together\n");
            return;
        }

        conn_consume(&c, hdr_end);

        if (te_chunked) {
            int too_large = 0;
            if (drain_chunked_body(&c, &too_large) != 0) {
                if (too_large) {
                    send_error(fd, 413, "Content Too Large", NULL, "chunked body too large\n");
                } else {
                    send_error(fd, 400, "Bad Request", NULL, "bad chunked body\n");
                }
                return;
            }
        } else if (has_cl && cl_val > 0) {
            if (drain_fixed_body(&c, (size_t)cl_val) != 0) {
                return;
            }
        }

        int keep_alive = is_11 ? !conn_close : conn_keep;
        if (req_num + 1 >= MAX_REQS_CONN) {
            keep_alive = 0;
        }

        int is_get = ci_eq(method, "GET");
        int is_head = ci_eq(method, "HEAD");
        if (!is_get && !is_head) {
            send_error(fd, 405, "Method Not Allowed", "Allow: GET, HEAD\r\n", "method not allowed\n");
            return;
        }

        if (strcmp(target, "/") == 0) {
            const char *html = "<!doctype html><html><body><h1>SETU HTTP/1.1</h1></body></html>\n";
            if (send_response(fd, 200, "OK", "text/html; charset=utf-8",
                              NULL, html, strlen(html), is_head, keep_alive) != 0) {
                return;
            }
        } else if (strcmp(target, "/healthz") == 0) {
            const char *ok = "ok";
            if (send_response(fd, 200, "OK", "text/plain; charset=utf-8",
                              NULL, ok, strlen(ok), is_head, keep_alive) != 0) {
                return;
            }
        } else {
            send_error(fd, 404, "Not Found", NULL, "not found\n");
            return;
        }

        if (!keep_alive) {
            return;
        }
    }
}

int main(int argc, char **argv) {
    const char *port = (argc >= 2) ? argv[1] : "8080";
    const char *bind_addr = (argc >= 3) ? argv[2] : "127.0.0.1";
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [port] [bind_addr]\n", argc > 0 ? argv[0] : "http_server");
        return 2;
    }

    if (net_init() != 0) {
        fprintf(stderr, "http_server: net_init failed\n");
        return 1;
    }

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int gai = getaddrinfo(bind_addr, port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "http_server: getaddrinfo: %s\n", gai_strerror(gai));
        net_cleanup();
        return 1;
    }

    sock_t srv = SETU_BAD_SOCK;
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        sock_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == SETU_BAD_SOCK) {
            continue;
        }
        int yes = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
        if (bind(s, ai->ai_addr, (sock_len_t)ai->ai_addrlen) == 0 && listen(s, 16) == 0) {
            srv = s;
            break;
        }
        sock_close(s);
    }
    freeaddrinfo(res);

    if (srv == SETU_BAD_SOCK) {
        fprintf(stderr, "http_server: bind/listen failed on %s:%s (errno=%d)\n",
                bind_addr, port, sock_errno());
        net_cleanup();
        return 1;
    }

    printf("http_server listening on %s:%s\n", bind_addr, port);
    fflush(stdout);

    for (;;) {
        sock_t cli = accept(srv, NULL, NULL);
        if (cli == SETU_BAD_SOCK) {
            continue;
        }
        handle_connection(cli);
        shutdown(cli, SETU_SHUT_WR);
        sock_close(cli);
    }

    sock_close(srv);
    net_cleanup();
    return 0;
}
