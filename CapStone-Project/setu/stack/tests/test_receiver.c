/* test_receiver.c - Unit tests for CP3 TCP Receiver.
 * Purpose: verify SYN, SYN+payload, pre-SYN ignore, out-of-order ackno, FIN after gap,
 *          duplicates/overlaps, 32-bit seqno wrap, window shrink/reopen, and 1 MB shuffle.
 * Usage:   ctest --test-dir build -C Debug (or run ./test_receiver directly).
 * Status:  written, build not run.
 */
#include "../src/receiver.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void drain_all(bytestream *bs, uint8_t *out, size_t *out_len, size_t max_out) {
    while (bs_len(bs) > 0) {
        char tmp[1024];
        size_t got = bs_peek(bs, tmp, sizeof(tmp));
        assert(*out_len + got <= max_out);
        memcpy(out + *out_len, tmp, got);
        *out_len += got;
        bs_pop(bs, got);
    }
}

static void test_syn_and_data(void) {
    bytestream bs;
    receiver rx;
    assert(bs_init(&bs, 64));
    assert(receiver_init(&rx, 64));

    uint32_t ack = 0;
    assert(!receiver_ackno(&rx, &ack));
    assert(!receiver_on_segment(&rx, &bs, 1000, false, false, (const uint8_t *)"abc", 3));

    assert(receiver_on_segment(&rx, &bs, 1000, true, false, NULL, 0));
    assert(receiver_ackno(&rx, &ack) && ack == 1001);
    assert(!receiver_on_segment(&rx, &bs, 2000, true, false, NULL, 0));

    assert(receiver_on_segment(&rx, &bs, 1001, false, true, (const uint8_t *)"hello", 5));
    assert(receiver_ackno(&rx, &ack) && ack == 1007);
    assert(bs.closed);

    uint8_t out[32];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 5 && memcmp(out, "hello", 5) == 0);
    assert(bs_finished(&bs));

    receiver_free(&rx);
    bs_free(&bs);
    printf("PASS: syn_and_data\n");
}

static void test_syn_with_payload(void) {
    bytestream bs;
    receiver rx;
    assert(bs_init(&bs, 64));
    assert(receiver_init(&rx, 64));

    assert(receiver_on_segment(&rx, &bs, 500, true, false, (const uint8_t *)"fast", 4));
    uint32_t ack = 0;
    assert(receiver_ackno(&rx, &ack) && ack == 505);

    uint8_t out[16];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 4 && memcmp(out, "fast", 4) == 0);

    receiver_free(&rx);
    bs_free(&bs);
    printf("PASS: syn_with_payload\n");
}

static void test_ooo_and_fin_after_gap(void) {
    bytestream bs;
    receiver rx;
    assert(bs_init(&bs, 64));
    assert(receiver_init(&rx, 64));

    uint32_t ack = 0;
    assert(receiver_on_segment(&rx, &bs, 100, true, false, NULL, 0));
    assert(receiver_on_segment(&rx, &bs, 104, false, true, (const uint8_t *)"DEF", 3));
    assert(receiver_ackno(&rx, &ack) && ack == 101);
    assert(!bs.closed);

    assert(receiver_on_segment(&rx, &bs, 102, false, false, (const uint8_t *)"BC", 2));
    assert(receiver_ackno(&rx, &ack) && ack == 101);

    /* Duplicate/overlapping segment. */
    assert(receiver_on_segment(&rx, &bs, 101, false, false, (const uint8_t *)"ABC", 3));
    assert(receiver_ackno(&rx, &ack) && ack == 108);
    assert(bs.closed);

    uint8_t out[16];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 6 && memcmp(out, "ABCDEF", 6) == 0);

    receiver_free(&rx);
    bs_free(&bs);
    printf("PASS: ooo_and_fin_after_gap\n");
}

static void test_wrap_past_u32(void) {
    bytestream bs;
    receiver rx;
    assert(bs_init(&bs, 1024));
    assert(receiver_init(&rx, 1024));

    uint32_t isn = 0xFFFFFF00u;
    assert(receiver_on_segment(&rx, &bs, isn, true, false, NULL, 0));

    uint8_t buf[512];
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t)(i & 0xffu);
    }
    assert(receiver_on_segment(&rx, &bs, isn + 1u + 256u, false, true, buf + 256, 256));
    uint32_t ack = 0;
    assert(receiver_ackno(&rx, &ack) && ack == isn + 1u);

    assert(receiver_on_segment(&rx, &bs, isn + 1u, false, false, buf, 256));
    assert(receiver_ackno(&rx, &ack) && ack == (uint32_t)(isn + 1u + 512u + 1u));
    assert(bs.closed);

    uint8_t out[512];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 512 && memcmp(out, buf, 512) == 0);

    receiver_free(&rx);
    bs_free(&bs);
    printf("PASS: wrap_past_u32\n");
}

static void test_window_shrink_and_reopen(void) {
    bytestream bs;
    receiver rx;
    assert(bs_init(&bs, 4));
    assert(receiver_init(&rx, 4));

    assert(receiver_on_segment(&rx, &bs, 10, true, false, NULL, 0));
    assert(receiver_window(&rx, &bs) == 4);

    assert(receiver_on_segment(&rx, &bs, 11, false, false, (const uint8_t *)"ABCD", 4));
    assert(receiver_window(&rx, &bs) == 0);

    /* Beyond window -> ignored. */
    assert(!receiver_on_segment(&rx, &bs, 15, false, false, (const uint8_t *)"EF", 2));

    bs_pop(&bs, 2);
    assert(receiver_window(&rx, &bs) == 2);
    assert(receiver_on_segment(&rx, &bs, 15, false, true, (const uint8_t *)"EF", 2));

    uint8_t out[8];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 4 && memcmp(out, "CDEF", 4) == 0);
    assert(bs_finished(&bs));

    receiver_free(&rx);
    bs_free(&bs);
    printf("PASS: window_shrink_and_reopen\n");
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void test_1mb_shuffle(void) {
    const size_t total = 1024u * 1024u;
    uint8_t *src = (uint8_t *)malloc(total);
    uint8_t *dst = (uint8_t *)malloc(total);
    assert(src && dst);

    uint32_t rng = 0x12345678u;
    for (size_t i = 0; i < total; i++) {
        src[i] = (uint8_t)(xorshift32(&rng) & 0xffu);
    }

    typedef struct { size_t off; size_t len; bool fin; } seg_t;
    size_t cap_s = 2048;
    size_t nsegs = 0;
    seg_t *segs = (seg_t *)malloc(cap_s * sizeof(seg_t));
    assert(segs);

    size_t off = 0;
    while (off < total) {
        size_t slen = 1u + (size_t)(xorshift32(&rng) % 1460u);
        if (off + slen > total) {
            slen = total - off;
        }
        if (nsegs == cap_s) {
            cap_s *= 2;
            segs = (seg_t *)realloc(segs, cap_s * sizeof(seg_t));
            assert(segs);
        }
        segs[nsegs++] = (seg_t){ .off = off, .len = slen, .fin = (off + slen == total) };
        off += slen;
    }

    for (size_t i = nsegs - 1; i > 0; i--) {
        size_t j = (size_t)(xorshift32(&rng) % (uint32_t)(i + 1));
        seg_t tmp = segs[i];
        segs[i] = segs[j];
        segs[j] = tmp;
    }

    bytestream bs;
    receiver rx;
    assert(bs_init(&bs, 65536));
    assert(receiver_init(&rx, 65536));

    uint32_t isn = 0xFFFFFF80u;
    assert(receiver_on_segment(&rx, &bs, isn, true, false, NULL, 0));

    size_t dst_len = 0;
    for (int pass = 0; pass < 64 && !bs_finished(&bs); pass++) {
        for (size_t i = 0; i < nsegs; i++) {
            uint32_t seq = tcp_wrap((uint64_t)segs[i].off + 1ULL, isn);
            receiver_on_segment(&rx, &bs, seq, false, segs[i].fin,
                                src + segs[i].off, segs[i].len);
            drain_all(&bs, dst, &dst_len, total);
        }
    }

    assert(bs_finished(&bs));
    assert(dst_len == total);
    assert(memcmp(src, dst, total) == 0);

    free(segs);
    free(src);
    free(dst);
    receiver_free(&rx);
    bs_free(&bs);
    printf("PASS: 1mb_shuffle\n");
}

int main(void) {
    test_syn_and_data();
    test_syn_with_payload();
    test_ooo_and_fin_after_gap();
    test_wrap_past_u32();
    test_window_shrink_and_reopen();
    test_1mb_shuffle();
    printf("ALL RECEIVER TESTS PASSED\n");
    return 0;
}
