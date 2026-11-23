#include "platform.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int send_all(sock_t s, const char *buf, size_t len) {
    size_t sent = 0;
    /* TCP send() may accept fewer bytes than requested; loop until all bytes are sent. */
    while (sent < len) {
#ifdef _WIN32
        int n = send(s, buf + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(s, buf + sent, len - sent, 0);
#endif
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s <host> <path> [port]\n", argc > 0 ? argv[0] : "webget");
        return 2;
    }

    const char *host = argv[1];
    const char *path = argv[2];
    const char *port = (argc == 4) ? argv[3] : "80";

    if (net_init() != 0) {
        fprintf(stderr, "webget: net_init failed\n");
        return 1;
    }

    int rc = 1;
    sock_t sock = SETU_BAD_SOCK;
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "webget: getaddrinfo(%s:%s): %s\n", host, port, gai_strerror(gai));
        goto cleanup;
    }

    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        sock_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == SETU_BAD_SOCK) {
            continue;
        }
        if (connect(s, ai->ai_addr, (sock_len_t)ai->ai_addrlen) == 0) {
            sock = s;
            break;
        }
        sock_close(s);
    }
    freeaddrinfo(res);
    res = NULL;

    if (sock == SETU_BAD_SOCK) {
        fprintf(stderr, "webget: failed to connect to %s:%s (errno=%d)\n", host, port, sock_errno());
        goto cleanup;
    }

    char req[4096];
    /* Connection: close asks the server to send FIN after the response so recv() == 0 terminates. */
    int req_len = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                           path, host);
    if (req_len < 0 || (size_t)req_len >= sizeof(req)) {
        fprintf(stderr, "webget: request too long for buffer\n");
        goto cleanup;
    }

    if (send_all(sock, req, (size_t)req_len) != 0) {
        fprintf(stderr, "webget: send failed (errno=%d)\n", sock_errno());
        goto cleanup;
    }

    char buf[8192];
    uint64_t total = 0;
    for (;;) {
#ifdef _WIN32
        int n = recv(sock, buf, (int)sizeof(buf), 0);
#else
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
#endif
        /* recv() returning 0 means the peer sent FIN (clean end of stream). */
        if (n == 0) {
            break;
        }
        if (n < 0) {
            fprintf(stderr, "webget: recv failed (errno=%d)\n", sock_errno());
            goto cleanup;
        }
        if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n) {
            fprintf(stderr, "webget: fwrite to stdout failed\n");
            goto cleanup;
        }
        total += (uint64_t)n;
    }

    fprintf(stderr, "[webget] %" PRIu64 " bytes received\n", total);
    rc = 0;

cleanup:
    if (sock != SETU_BAD_SOCK) {
        sock_close(sock);
    }
    net_cleanup();
    return rc;
}
