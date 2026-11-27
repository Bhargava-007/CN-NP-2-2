/* test_tcp_connection.c - Unit tests for tcp_connection state machine.
 * Purpose: verify two back-to-back tcp_conn instances over 100 KB bidirectional,
 *          1 MB 5% loss, 1 MB reordering, wrong port / corruption, RST, and empty close.
 * Usage:   ctest --test-dir build -C Debug (or run ./test_tcp_connection directly).
 * Status:  written, build not run.
 */
#include "../src/tcp_connection.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void test_bidir_100k(void) {
    const size_t total = 100u * 1024u;
    uint8_t *a_src = (uint8_t *)malloc(total);
    uint8_t *b_src = (uint8_t *)malloc(total);
    uint8_t *a_dst = (uint8_t *)malloc(total);
    uint8_t *b_dst = (uint8_t *)malloc(total);
    assert(a_src && b_src && a_dst && b_dst);

    for (size_t i = 0; i < total; i++) {
        a_src[i] = (uint8_t)((i * 7u + 1u) & 0xffu);
        b_src[i] = (uint8_t)((i * 13u + 5u) & 0xffu);
    }

    tcp_conn a, b;
    assert(tcp_conn_init(&a, 0x0a000001u, 40000, 0x0a000002u, 9000, 1000, 1460));
    assert(tcp_conn_init(&b, 0x0a000002u, 9000, 0, 0, 2000, 1460));
    tcp_conn_connect(&a);
    tcp_conn_listen(&b);

    size_t a_sent = 0, b_sent = 0, a_got = 0, b_got = 0;
    uint64_t now = 0;
    uint8_t pkt[2048];
    int iter = 0;

    while (!(tcp_conn_done(&a) && tcp_conn_done(&b)) && iter < 200000) {
        iter++;
        if (a_sent < total) {
            a_sent += tcp_conn_write_input(&a, (const char *)(a_src + a_sent), total - a_sent);
            if (a_sent == total) tcp_conn_close_input(&a);
        }
        if (b_sent < total) {
            b_sent += tcp_conn_write_input(&b, (const char *)(b_src + b_sent), total - b_sent);
            if (b_sent == total) tcp_conn_close_input(&b);
        }

        size_t n;
        while ((n = tcp_conn_poll(&a, now, pkt, sizeof(pkt))) > 0) {
            tcp_conn_on_packet(&b, pkt, n, now + 1);
        }
        while ((n = tcp_conn_poll(&b, now, pkt, sizeof(pkt))) > 0) {
            tcp_conn_on_packet(&a, pkt, n, now + 1);
        }

        char tmp[4096];
        size_t r;
        while ((r = tcp_conn_read_output(&a, tmp, sizeof(tmp))) > 0) {
            assert(a_got + r <= total);
            memcpy(a_dst + a_got, tmp, r);
            a_got += r;
        }
        while ((r = tcp_conn_read_output(&b, tmp, sizeof(tmp))) > 0) {
            assert(b_got + r <= total);
            memcpy(b_dst + b_got, tmp, r);
            b_got += r;
        }
        now += 1;
    }

    assert(iter < 200000);
    assert(tcp_conn_done(&a) && tcp_conn_done(&b));
    assert(a_got == total && b_got == total);
    assert(memcmp(a_dst, b_src, total) == 0);
    assert(memcmp(b_dst, a_src, total) == 0);

    free(a_src); free(b_src); free(a_dst); free(b_dst);
    tcp_conn_free(&a); tcp_conn_free(&b);
    printf("PASS: bidir_100k\n");
}

static void test_lossy_1mb(void) {
    const size_t total = 1024u * 1024u;
    uint8_t *src = (uint8_t *)malloc(total);
    uint8_t *dst = (uint8_t *)malloc(total);
    assert(src && dst);

    uint32_t rng = 0x11223344u;
    for (size_t i = 0; i < total; i++) {
        src[i] = (uint8_t)(xorshift32(&rng) & 0xffu);
    }

    tcp_conn a, b;
    assert(tcp_conn_init(&a, 0x0a000001u, 40000, 0x0a000002u, 9000, 0xFFFFFF00u, 1460));
    assert(tcp_conn_init(&b, 0x0a000002u, 9000, 0, 0, 5000, 1460));
    tcp_conn_connect(&a);
    tcp_conn_listen(&b);
    tcp_conn_close_input(&b);

    size_t sent = 0, got = 0;
    uint64_t now = 0;
    uint8_t pkt[2048];
    int iter = 0;
    const int max_iters = 1000000;

    while (!(tcp_conn_done(&a) && tcp_conn_done(&b)) && iter < max_iters) {
        iter++;
        if (sent < total) {
            sent += tcp_conn_write_input(&a, (const char *)(src + sent), total - sent);
            if (sent == total) tcp_conn_close_input(&a);
        }

        bool moved = false;
        size_t n;
        while ((n = tcp_conn_poll(&a, now, pkt, sizeof(pkt))) > 0) {
            moved = true;
            if ((xorshift32(&rng) % 20u) != 0u) {
                tcp_conn_on_packet(&b, pkt, n, now + 1);
            }
        }
        while ((n = tcp_conn_poll(&b, now, pkt, sizeof(pkt))) > 0) {
            moved = true;
            if ((xorshift32(&rng) % 20u) != 0u) {
                tcp_conn_on_packet(&a, pkt, n, now + 1);
            }
        }

        char tmp[4096];
        size_t r;
        while ((r = tcp_conn_read_output(&b, tmp, sizeof(tmp))) > 0) {
            assert(got + r <= total);
            memcpy(dst + got, tmp, r);
            got += r;
        }

        if (!moved) {
            uint64_t da = tcp_conn_next_deadline(&a);
            uint64_t db = tcp_conn_next_deadline(&b);
            uint64_t next_dl = da < db ? da : db;
            now = (next_dl != UINT64_MAX && next_dl > now) ? next_dl : (now + 1);
        } else {
            now += 1;
        }
    }

    assert(iter < max_iters);
    assert(tcp_conn_done(&a) && tcp_conn_done(&b));
    assert(got == total && memcmp(src, dst, total) == 0);

    free(src); free(dst);
    tcp_conn_free(&a); tcp_conn_free(&b);
    printf("PASS: lossy_1mb\n");
}

static void test_reorder_1mb(void) {
    const size_t total = 1024u * 1024u;
    uint8_t *src = (uint8_t *)malloc(total);
    uint8_t *dst = (uint8_t *)malloc(total);
    assert(src && dst);

    uint32_t rng = 0x55667788u;
    for (size_t i = 0; i < total; i++) {
        src[i] = (uint8_t)(xorshift32(&rng) & 0xffu);
    }

    tcp_conn a, b;
    assert(tcp_conn_init(&a, 0x0a000001u, 40000, 0x0a000002u, 9000, 1000, 1460));
    assert(tcp_conn_init(&b, 0x0a000002u, 9000, 0, 0, 2000, 1460));
    tcp_conn_connect(&a);
    tcp_conn_listen(&b);
    tcp_conn_close_input(&b);

    typedef struct { uint8_t data[1600]; size_t len; } qpkt_t;
    qpkt_t *q = (qpkt_t *)malloc(128 * sizeof(qpkt_t));
    assert(q);

    size_t sent = 0, got = 0;
    uint64_t now = 0;
    int iter = 0;

    while (!(tcp_conn_done(&a) && tcp_conn_done(&b)) && iter < 500000) {
        iter++;
        if (sent < total) {
            sent += tcp_conn_write_input(&a, (const char *)(src + sent), total - sent);
            if (sent == total) tcp_conn_close_input(&a);
        }

        size_t nq = 0;
        size_t n;
        while (nq < 128 && (n = tcp_conn_poll(&a, now, q[nq].data, sizeof(q[nq].data))) > 0) {
            q[nq++].len = n;
        }
        for (size_t i = 0; i + 1 < nq; i += 2) {
            if ((xorshift32(&rng) & 1u) != 0u) {
                qpkt_t tmp = q[i];
                q[i] = q[i + 1];
                q[i + 1] = tmp;
            }
        }
        for (size_t i = 0; i < nq; i++) {
            tcp_conn_on_packet(&b, q[i].data, q[i].len, now + 1);
        }

        uint8_t pkt[2048];
        while ((n = tcp_conn_poll(&b, now, pkt, sizeof(pkt))) > 0) {
            tcp_conn_on_packet(&a, pkt, n, now + 1);
        }

        char tmp[4096];
        size_t r;
        while ((r = tcp_conn_read_output(&b, tmp, sizeof(tmp))) > 0) {
            assert(got + r <= total);
            memcpy(dst + got, tmp, r);
            got += r;
        }
        now += 1;
    }

    assert(iter < 500000);
    assert(tcp_conn_done(&a) && tcp_conn_done(&b));
    assert(got == total && memcmp(src, dst, total) == 0);

    free(q); free(src); free(dst);
    tcp_conn_free(&a); tcp_conn_free(&b);
    printf("PASS: reorder_1mb\n");
}

static void test_wrong_port_and_corruption(void) {
    tcp_conn b;
    assert(tcp_conn_init(&b, 0x0a000002u, 9000, 0, 0, 2000, 1460));
    tcp_conn_listen(&b);

    ip_tcp_seg s = {
        .src_ip = 0x0a000001u,
        .dst_ip = 0x0a000002u,
        .sport = 40000,
        .dport = 9001, /* wrong port */
        .seq = 100,
        .syn = true,
        .window = 1000,
        .mss = 1460,
    };
    uint8_t pkt[64];
    size_t n = ip_tcp_build(pkt, sizeof(pkt), &s, 1);
    tcp_conn_on_packet(&b, pkt, n, 1);
    assert(!b.peer_syn_seen);

    s.dport = 9000;
    n = ip_tcp_build(pkt, sizeof(pkt), &s, 2);
    pkt[25] ^= 0xffu; /* corrupt TCP seq */
    tcp_conn_on_packet(&b, pkt, n, 2);
    assert(!b.peer_syn_seen && b.n_parse_drops == 1);

    tcp_conn_free(&b);
    printf("PASS: wrong_port_and_corruption\n");
}

static void test_rst_stops_connection(void) {
    tcp_conn a, b;
    assert(tcp_conn_init(&a, 0x0a000001u, 40000, 0x0a000002u, 9000, 100, 1460));
    assert(tcp_conn_init(&b, 0x0a000002u, 9000, 0, 0, 200, 1460));
    tcp_conn_connect(&a);
    tcp_conn_listen(&b);

    uint8_t pkt[64];
    size_t n = tcp_conn_poll(&a, 0, pkt, sizeof(pkt));
    assert(n > 0);
    tcp_conn_on_packet(&b, pkt, n, 1);

    ip_tcp_seg rst = {
        .src_ip = 0x0a000002u,
        .dst_ip = 0x0a000001u,
        .sport = 9000,
        .dport = 40000,
        .seq = 200,
        .rst = true,
    };
    n = ip_tcp_build(pkt, sizeof(pkt), &rst, 1);
    tcp_conn_on_packet(&a, pkt, n, 2);
    assert(a.rst && tcp_conn_done(&a));

    tcp_conn_free(&a); tcp_conn_free(&b);
    printf("PASS: rst_stops_connection\n");
}

static void test_simultaneous_empty_close(void) {
    tcp_conn a, b;
    assert(tcp_conn_init(&a, 0x0a000001u, 40000, 0x0a000002u, 9000, 100, 1460));
    assert(tcp_conn_init(&b, 0x0a000002u, 9000, 0, 0, 200, 1460));
    tcp_conn_connect(&a);
    tcp_conn_listen(&b);
    tcp_conn_close_input(&a);
    tcp_conn_close_input(&b);

    uint8_t pkt[256];
    uint64_t now = 0;
    int iter = 0;
    while (!(tcp_conn_done(&a) && tcp_conn_done(&b)) && iter < 1000) {
        iter++;
        size_t n;
        while ((n = tcp_conn_poll(&a, now, pkt, sizeof(pkt))) > 0) {
            tcp_conn_on_packet(&b, pkt, n, now + 1);
        }
        while ((n = tcp_conn_poll(&b, now, pkt, sizeof(pkt))) > 0) {
            tcp_conn_on_packet(&a, pkt, n, now + 1);
        }
        now += 1;
    }
    assert(iter < 1000);
    assert(tcp_conn_done(&a) && tcp_conn_done(&b));

    tcp_conn_free(&a); tcp_conn_free(&b);
    printf("PASS: simultaneous_empty_close\n");
}

int main(void) {
    test_bidir_100k();
    test_lossy_1mb();
    test_reorder_1mb();
    test_wrong_port_and_corruption();
    test_rst_stops_connection();
    test_simultaneous_empty_close();
    printf("ALL TCP_CONNECTION TESTS PASSED\n");
    return 0;
}
