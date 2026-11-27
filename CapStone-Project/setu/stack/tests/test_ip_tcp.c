/* test_ip_tcp.c - Unit tests for IPv4 + TCP header parser, builder, and checksum.
 * Purpose: verify RFC 1071 checksums, SYN+MSS round-trip, odd/even payloads,
 *          corruption detection, malformed header rejections, cap bounds, and wrap.
 * Usage:   ctest --test-dir build -C Debug (or run ./test_ip_tcp directly).
 * Status:  written, build not run.
 */
#include "../src/ip_tcp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_rfc1071_and_odd(void) {
    /* Vector from RFC 1071: 00 01 f2 03 f4 f5 f6 f7 -> sum 0xddf2 -> cksum 0x220d */
    const uint8_t v1[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    assert(inet_checksum(v1, sizeof(v1), 0) == 0x220du);

    /* Odd-length 3 bytes: 01 02 03 -> words 0x0102 + 0x0300 = 0x0402 -> ~0x0402 = 0xfbfd */
    const uint8_t v2[] = {0x01, 0x02, 0x03};
    assert(inet_checksum(v2, sizeof(v2), 0) == 0xfbfdu);
    printf("PASS: rfc1071_and_odd\n");
}

static void test_syn_mss_roundtrip(void) {
    ip_tcp_seg s = {
        .src_ip = 0x0a143008u,
        .dst_ip = 0x0a142001u,
        .sport = 12345,
        .dport = 8080,
        .seq = 0x11223344u,
        .ack = 0,
        .syn = true,
        .window = 32768,
        .mss = 1460,
        .ttl = 64,
    };
    uint8_t buf[128];
    size_t n = ip_tcp_build(buf, sizeof(buf), &s, 42);
    assert(n == 44);

    ip_tcp_seg out;
    assert(ip_tcp_parse(buf, n, &out));
    assert(out.src_ip == s.src_ip && out.dst_ip == s.dst_ip);
    assert(out.sport == s.sport && out.dport == s.dport);
    assert(out.seq == s.seq && out.ack == s.ack);
    assert(out.syn && !out.ackf && !out.fin && !out.rst && !out.psh);
    assert(out.window == s.window && out.mss == 1460 && out.ttl == 64);
    assert(out.payload_len == 0 && out.payload == NULL);
    printf("PASS: syn_mss_roundtrip\n");
}

static void test_data_payload_sizes(void) {
    const size_t sizes[] = {0, 1, 2, 1000};
    uint8_t payload[1000];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)((i * 37u + 13u) & 0xffu);
    }
    for (size_t idx = 0; idx < sizeof(sizes) / sizeof(sizes[0]); idx++) {
        size_t plen = sizes[idx];
        ip_tcp_seg s = {
            .src_ip = 0x0a000001u,
            .dst_ip = 0x0a000002u,
            .sport = 5000,
            .dport = 80,
            .seq = 100,
            .ack = 200,
            .ackf = true,
            .psh = (plen > 0),
            .window = 4096,
            .payload = (plen > 0) ? payload : NULL,
            .payload_len = plen,
        };
        uint8_t buf[1200];
        size_t n = ip_tcp_build(buf, sizeof(buf), &s, 1);
        assert(n == 40 + plen);

        ip_tcp_seg out;
        assert(ip_tcp_parse(buf, n, &out));
        assert(out.payload_len == plen);
        if (plen > 0) {
            assert(memcmp(out.payload, payload, plen) == 0);
        }
    }
    printf("PASS: data_payload_sizes\n");
}

static void test_corruption_detection(void) {
    const uint8_t msg[] = "hello";
    ip_tcp_seg s = {
        .src_ip = 0x0a000001u,
        .dst_ip = 0x0a000002u,
        .sport = 1111,
        .dport = 2222,
        .seq = 1,
        .ack = 2,
        .ackf = true,
        .window = 1024,
        .payload = msg,
        .payload_len = 5,
    };
    uint8_t orig[64], tmp[64];
    size_t n = ip_tcp_build(orig, sizeof(orig), &s, 7);
    assert(n == 45);

    ip_tcp_seg out;
    memcpy(tmp, orig, n);
    tmp[8] ^= 0x01u; /* IP TTL byte */
    assert(!ip_tcp_parse(tmp, n, &out));

    memcpy(tmp, orig, n);
    tmp[24] ^= 0x01u; /* TCP seq byte */
    assert(!ip_tcp_parse(tmp, n, &out));

    memcpy(tmp, orig, n);
    tmp[42] ^= 0x01u; /* payload byte */
    assert(!ip_tcp_parse(tmp, n, &out));
    printf("PASS: corruption_detection\n");
}

static void test_rejections(void) {
    ip_tcp_seg s = {
        .src_ip = 0x0a000001u,
        .dst_ip = 0x0a000002u,
        .sport = 100,
        .dport = 200,
        .syn = true,
        .mss = 1460,
        .window = 1000,
    };
    uint8_t orig[64], tmp[64];
    size_t n = ip_tcp_build(orig, sizeof(orig), &s, 1);
    assert(n == 44);

    ip_tcp_seg out;
    /* Version 6 */
    memcpy(tmp, orig, n);
    tmp[0] = 0x65u;
    tmp[10] = 0; tmp[11] = 0;
    uint16_t c = inet_checksum(tmp, 20, 0);
    tmp[10] = (uint8_t)(c >> 8); tmp[11] = (uint8_t)(c & 0xffu);
    assert(!ip_tcp_parse(tmp, n, &out));

    /* IHL 4 */
    memcpy(tmp, orig, n);
    tmp[0] = 0x44u;
    assert(!ip_tcp_parse(tmp, n, &out));

    /* Protocol 17 */
    memcpy(tmp, orig, n);
    tmp[9] = 17u;
    tmp[10] = 0; tmp[11] = 0;
    c = inet_checksum(tmp, 20, 0);
    tmp[10] = (uint8_t)(c >> 8); tmp[11] = (uint8_t)(c & 0xffu);
    assert(!ip_tcp_parse(tmp, n, &out));

    /* Fragment (MF set) */
    memcpy(tmp, orig, n);
    tmp[6] = 0x20u;
    tmp[10] = 0; tmp[11] = 0;
    c = inet_checksum(tmp, 20, 0);
    tmp[10] = (uint8_t)(c >> 8); tmp[11] = (uint8_t)(c & 0xffu);
    assert(!ip_tcp_parse(tmp, n, &out));

    /* total_len > len */
    assert(!ip_tcp_parse(orig, n - 1, &out));

    /* Truncated packet (< 40) */
    assert(!ip_tcp_parse(orig, 30, &out));

    /* Option with length byte 0 */
    memcpy(tmp, orig, n);
    tmp[41] = 0; /* MSS option length = 0 */
    tmp[36] = 0; tmp[37] = 0;
    uint32_t seed = (s.src_ip >> 16) + (s.src_ip & 0xffffu) +
                    (s.dst_ip >> 16) + (s.dst_ip & 0xffffu) + 6u + 24u;
    uint16_t tc = inet_checksum(tmp + 20, 24, seed);
    tmp[36] = (uint8_t)(tc >> 8); tmp[37] = (uint8_t)(tc & 0xffu);
    assert(!ip_tcp_parse(tmp, n, &out));

    /* Option with length overrun */
    memcpy(tmp, orig, n);
    tmp[41] = 10; /* exceeds tcp_hlen=24 */
    tmp[36] = 0; tmp[37] = 0;
    tc = inet_checksum(tmp + 20, 24, seed);
    tmp[36] = (uint8_t)(tc >> 8); tmp[37] = (uint8_t)(tc & 0xffu);
    assert(!ip_tcp_parse(tmp, n, &out));

    printf("PASS: rejections\n");
}

static void test_cap_too_small_and_wrap(void) {
    ip_tcp_seg s = {
        .src_ip = 0x0a000001u,
        .dst_ip = 0x0a000002u,
        .sport = 1,
        .dport = 2,
        .seq = 0xFFFFFFFFu,
        .ack = 0u,
        .ackf = true,
        .window = 512,
    };
    uint8_t small[40];
    small[39] = 0xAAu;
    assert(ip_tcp_build(small, 39, &s, 1) == 0);
    assert(small[39] == 0xAAu);

    uint8_t full[40];
    assert(ip_tcp_build(full, sizeof(full), &s, 1) == 40);
    ip_tcp_seg out;
    assert(ip_tcp_parse(full, 40, &out));
    assert(out.seq == 0xFFFFFFFFu && out.ack == 0u);
    printf("PASS: cap_too_small_and_wrap\n");
}

static void test_hand_computed_syn(void) {
    /*
     * Hand-computed IPv4+TCP SYN (44 bytes):
     * IPv4: 4500 002c 0001 4000 4006 [cksum] 0a00 0001 0a00 0002
     *   Sum without cksum = 0x4500+0x002c+0x0001+0x4000+0x4006+0x0a00+0x0001+0x0a00+0x0002 = 0xd936
     *   IP cksum = ~0xd936 = 0x26c9
     * TCP: 3039 0050 0000 0001 0000 0000 6002 ffff [cksum] 0000 0204 05b4
     *   Pseudo = 0x0a00+0x0001+0x0a00+0x0002+0x0006+0x0018 = 0x1421
     *   TCP+pseudo = 0x1421+0x3039+0x0050+0x0001+0x6002+0xffff+0x0204+0x05b4 = 0x1ac64 -> 0xac65
     *   TCP cksum = ~0xac65 = 0x539a
     */
    const uint8_t pkt[44] = {
        0x45, 0x00, 0x00, 0x2c, 0x00, 0x01, 0x40, 0x00,
        0x40, 0x06, 0x26, 0xc9, 0x0a, 0x00, 0x00, 0x01,
        0x0a, 0x00, 0x00, 0x02,
        0x30, 0x39, 0x00, 0x50, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x60, 0x02, 0xff, 0xff,
        0x53, 0x9a, 0x00, 0x00, 0x02, 0x04, 0x05, 0xb4
    };
    ip_tcp_seg out;
    assert(ip_tcp_parse(pkt, sizeof(pkt), &out));
    assert(out.sport == 12345 && out.dport == 80);
    assert(out.seq == 1 && out.syn && out.mss == 1460);
    printf("PASS: hand_computed_syn\n");
}

int main(void) {
    test_rfc1071_and_odd();
    test_syn_mss_roundtrip();
    test_data_payload_sizes();
    test_corruption_detection();
    test_rejections();
    test_cap_too_small_and_wrap();
    test_hand_computed_syn();
    printf("ALL IP_TCP TESTS PASSED\n");
    return 0;
}
