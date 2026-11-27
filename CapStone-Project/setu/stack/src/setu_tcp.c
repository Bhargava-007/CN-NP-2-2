/* setu_tcp.c - the Interop Gate harness (week 6). Builds and runs inside the lab engine,
   driven from PowerShell:  Invoke-SetuInteropGate -Profile jitter -Direction send
   It opens TUN device tun144, and moves raw IPv4 datagrams between the kernel and tcp_conn.
   Status: written, build not run.                                                            */
#ifdef __linux__
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "tcp_connection.h"

#define LOCAL_IP_HOST  0xA9FE9009u /* 169.254.144.9 */
#define CONNECT_LPORT  40000u      /* Fixed local ephemeral port in connect mode */
#define TOTAL_IDLE_MS  60000ULL

static int tun_open(const char *name) {
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) { perror("open /dev/net/tun"); return -1; }
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;                 /* raw IP datagrams, no extra header */
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) { perror("TUNSETIFF"); close(fd); return -1; }
    return fd;
}

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ULL + (uint64_t)t.tv_nsec / 1000000ULL;
}

static int set_nonblocking(int fd) {
    int f = fcntl(fd, F_GETFL, 0);
    return (f < 0) ? -1 : fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

static int write_all_stdout(const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(STDOUT_FILENO, buf + off, len - off);
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

int main(int argc, char **argv) {
    int is_connect = 0;
    uint32_t rip = 0;
    uint16_t rport = 0;
    uint16_t lport = 0;

    if (argc == 4 && strcmp(argv[1], "connect") == 0) {
        struct in_addr a;
        if (inet_pton(AF_INET, argv[2], &a) != 1) {
            fprintf(stderr, "invalid IP: %s\n", argv[2]);
            return 2;
        }
        is_connect = 1;
        rip = ntohl(a.s_addr);
        rport = (uint16_t)atoi(argv[3]);
        lport = CONNECT_LPORT;
    } else if (argc == 3 && strcmp(argv[1], "listen") == 0) {
        lport = (uint16_t)atoi(argv[2]);
    } else {
        fprintf(stderr, "usage: setu_tcp connect <ip> <port> | listen <port>\n");
        return 2;
    }

    int tun = tun_open("tun144");
    if (tun < 0) {
        return 1;
    }
    set_nonblocking(tun);
    set_nonblocking(STDIN_FILENO);

    uint32_t isn = (uint32_t)(now_ms() & 0xffffffffu) ^ 0x5E701234u;
    tcp_conn conn;
    if (!tcp_conn_init(&conn, LOCAL_IP_HOST, lport, rip, rport, isn, 1460u)) {
        fprintf(stderr, "[setu_tcp] tcp_conn_init failed\n");
        close(tun);
        return 1;
    }
    if (is_connect) {
        tcp_conn_connect(&conn);
    } else {
        tcp_conn_listen(&conn);
    }

    fprintf(stderr, "[setu_tcp] tun144 open, our address 169.254.144.9, mode=%s\n", argv[1]);

    int stdin_eof = 0;
    uint64_t last_progress = now_ms();
    int rc = 0;
    uint8_t pkt[4096];

    for (;;) {
        uint64_t t_now = now_ms();
        uint64_t dl = tcp_conn_next_deadline(&conn);
        int timeout_ms = 1000;
        if (dl != UINT64_MAX) {
            if (dl <= t_now) {
                timeout_ms = 0;
            } else if (dl - t_now < (uint64_t)timeout_ms) {
                timeout_ms = (int)(dl - t_now);
            }
        }

        struct pollfd pfds[2];
        nfds_t nfds = 1;
        pfds[0].fd = tun;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        if (!stdin_eof && bs_remaining(&conn.in) > 0) {
            pfds[1].fd = STDIN_FILENO;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }

        poll(pfds, nfds, timeout_ms);
        t_now = now_ms();

        /* Read available packets from TUN. */
        for (;;) {
            ssize_t n = read(tun, pkt, sizeof(pkt));
            if (n > 0) {
                tcp_conn_on_packet(&conn, pkt, (size_t)n, t_now);
                last_progress = t_now;
            } else {
                break;
            }
        }

        /* Read stdin up to bs_remaining(&conn.in). */
        if (!stdin_eof && bs_remaining(&conn.in) > 0) {
            size_t rem = bs_remaining(&conn.in);
            char inbuf[4096];
            size_t want = rem < sizeof(inbuf) ? rem : sizeof(inbuf);
            ssize_t k = read(STDIN_FILENO, inbuf, want);
            if (k > 0) {
                tcp_conn_write_input(&conn, inbuf, (size_t)k);
                last_progress = t_now;
            } else if (k == 0) {
                stdin_eof = 1;
                tcp_conn_close_input(&conn);
                last_progress = t_now;
            }
        }

        /* Drain outgoing packets to TUN. */
        size_t out_len;
        while ((out_len = tcp_conn_poll(&conn, t_now, pkt, sizeof(pkt))) > 0) {
            ssize_t wn = write(tun, pkt, out_len);
            if (wn > 0) {
                last_progress = t_now;
            }
        }

        /* Drain received stream to stdout. */
        char outbuf[4096];
        size_t rbytes;
        while ((rbytes = tcp_conn_read_output(&conn, outbuf, sizeof(outbuf))) > 0) {
            if (write_all_stdout(outbuf, rbytes) != 0) {
                rc = 1;
                break;
            }
            last_progress = t_now;
        }
        if (rc != 0) {
            break;
        }

        if (conn.rst) {
            rc = 1;
            break;
        }
        if (tcp_conn_done(&conn) && bs_len(&conn.out) == 0) {
            rc = 0;
            break;
        }
        if (t_now - last_progress >= TOTAL_IDLE_MS) {
            rc = 4;
            break;
        }
    }

    fprintf(stderr, "[setu_tcp] sent=%llu retx=%llu parse_drops=%llu rc=%d\n",
            (unsigned long long)conn.n_segs_sent,
            (unsigned long long)conn.tx.n_retx_segs,
            (unsigned long long)conn.n_parse_drops,
            rc);

    tcp_conn_free(&conn);
    close(tun);
    return rc;
}
#else
#include <stdio.h>
int main(void) { puts("setu_tcp runs in the lab engine (Linux TUN). Use Invoke-SetuInteropGate from PowerShell."); return 0; }
#endif
