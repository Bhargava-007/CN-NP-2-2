/* test_reassembler.c - Unit tests for CP3 Reassembler.
 * Purpose: verify in-order, reverse-order, 100 KB random shuffle with overlaps/dups,
 *          window clipping, pending bound, empty/early FIN, cap=1, flow control, >2^32 indices.
 * Usage:   ctest --test-dir build -C Debug (or run ./test_reassembler directly).
 * Status:  written, build not run.
 */
#include "../src/reassembler.h"

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

static void test_in_order(void) {
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, 64));
    assert(reasm_init(&r, 64));

    assert(reasm_push(&r, &bs, 0, (const uint8_t *)"hello ", 6, false));
    assert(reasm_push(&r, &bs, 6, (const uint8_t *)"world", 5, true));
    assert(reasm_pending(&r) == 0);
    assert(reasm_next_index(&r) == 11);
    assert(bs.closed);

    uint8_t out[64];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 11 && memcmp(out, "hello world", 11) == 0);
    assert(bs_finished(&bs));

    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: in_order\n");
}

static void test_reverse_order(void) {
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, 64));
    assert(reasm_init(&r, 64));

    assert(reasm_push(&r, &bs, 6, (const uint8_t *)"world", 5, true));
    assert(reasm_pending(&r) == 5);
    assert(reasm_next_index(&r) == 0);
    assert(!bs.closed);

    assert(reasm_push(&r, &bs, 0, (const uint8_t *)"hello ", 6, false));
    assert(reasm_pending(&r) == 0);
    assert(reasm_next_index(&r) == 11);
    assert(bs.closed);

    uint8_t out[64];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 11 && memcmp(out, "hello world", 11) == 0);
    assert(bs_finished(&bs));

    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: reverse_order\n");
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void test_random_100k_shuffle(void) {
    const size_t total = 100u * 1024u;
    uint8_t *src = (uint8_t *)malloc(total);
    uint8_t *dst = (uint8_t *)malloc(total);
    assert(src && dst);

    uint32_t rng = 0x5E70C0DEu;
    for (size_t i = 0; i < total; i++) {
        src[i] = (uint8_t)(xorshift32(&rng) & 0xffu);
    }

    typedef struct { size_t off; size_t len; bool last; } seg_t;
    size_t seg_cap = 2048;
    size_t nsegs = 0;
    seg_t *segs = (seg_t *)malloc(seg_cap * sizeof(seg_t));
    assert(segs);

    size_t off = 0;
    while (off < total) {
        size_t slen = 1u + (size_t)(xorshift32(&rng) % 500u);
        if (off + slen > total) {
            slen = total - off;
        }
        if (nsegs == seg_cap) {
            seg_cap *= 2;
            segs = (seg_t *)realloc(segs, seg_cap * sizeof(seg_t));
            assert(segs);
        }
        segs[nsegs++] = (seg_t){ .off = off, .len = slen, .last = (off + slen == total) };
        /* Add occasional overlapping/duplicate segment. */
        if ((xorshift32(&rng) % 4u) == 0 && nsegs < seg_cap) {
            size_t back = (off >= 10) ? 10 : off;
            size_t dlen = slen + back;
            if (off - back + dlen <= total) {
                segs[nsegs++] = (seg_t){ .off = off - back, .len = dlen, .last = false };
            }
        }
        off += slen;
    }

    for (size_t i = nsegs - 1; i > 0; i--) {
        size_t j = (size_t)(xorshift32(&rng) % (uint32_t)(i + 1));
        seg_t tmp = segs[i];
        segs[i] = segs[j];
        segs[j] = tmp;
    }

    const size_t cap = 4096;
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, cap));
    assert(reasm_init(&r, cap));

    size_t dst_len = 0;
    /* Multiple passes simulate retransmission of segments dropped beyond the window. */
    for (int pass = 0; pass < 32 && !bs_finished(&bs); pass++) {
        for (size_t i = 0; i < nsegs; i++) {
            assert(reasm_push(&r, &bs, (uint64_t)segs[i].off,
                              src + segs[i].off, segs[i].len, segs[i].last));
            assert(reasm_pending(&r) <= cap);
            drain_all(&bs, dst, &dst_len, total);
        }
    }

    assert(bs_finished(&bs));
    assert(dst_len == total);
    assert(memcmp(src, dst, total) == 0);

    free(segs);
    free(src);
    free(dst);
    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: random_100k_shuffle\n");
}

static void test_beyond_window_drop_and_recover(void) {
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, 4));
    assert(reasm_init(&r, 4));

    assert(reasm_push(&r, &bs, 4, (const uint8_t *)"EFGH", 4, false));
    assert(reasm_pending(&r) == 0);
    assert(reasm_next_index(&r) == 0);

    assert(reasm_push(&r, &bs, 0, (const uint8_t *)"ABCD", 4, false));
    assert(reasm_next_index(&r) == 4);

    uint8_t out[16];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));

    assert(reasm_push(&r, &bs, 4, (const uint8_t *)"EFGH", 4, true));
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 8 && memcmp(out, "ABCDEFGH", 8) == 0);
    assert(bs_finished(&bs));

    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: beyond_window_drop_and_recover\n");
}

static void test_empty_fin_and_early_fin(void) {
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, 16));
    assert(reasm_init(&r, 16));
    assert(reasm_push(&r, &bs, 0, NULL, 0, true));
    assert(bs_finished(&bs));
    reasm_free(&r);
    bs_free(&bs);

    assert(bs_init(&bs, 16));
    assert(reasm_init(&r, 16));
    assert(reasm_push(&r, &bs, 5, NULL, 0, true));
    assert(!bs.closed);
    assert(reasm_push(&r, &bs, 0, (const uint8_t *)"12345", 5, false));
    assert(bs.closed);
    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: empty_fin_and_early_fin\n");
}

static void test_capacity_one(void) {
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, 1));
    assert(reasm_init(&r, 1));

    const char *msg = "0123456789";
    uint8_t out[16];
    size_t n = 0;
    for (size_t i = 0; i < 10; i++) {
        assert(reasm_push(&r, &bs, (uint64_t)i, (const uint8_t *)&msg[i], 1, i == 9));
        assert(reasm_pending(&r) <= 1);
        drain_all(&bs, out, &n, sizeof(out));
    }
    assert(n == 10 && memcmp(out, msg, 10) == 0);
    assert(bs_finished(&bs));

    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: capacity_one\n");
}

static void test_flow_control_small_bytestream(void) {
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, 3));
    assert(reasm_init(&r, 16));

    assert(reasm_push(&r, &bs, 0, (const uint8_t *)"ABCDEF", 6, true));
    assert(bs_len(&bs) == 3);
    assert(reasm_next_index(&r) == 3);
    assert(reasm_pending(&r) == 0);

    uint8_t out[16];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 3 && memcmp(out, "ABC", 3) == 0);

    assert(reasm_push(&r, &bs, 3, (const uint8_t *)"DEF", 3, true));
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 6 && memcmp(out, "ABCDEF", 6) == 0);
    assert(bs_finished(&bs));

    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: flow_control_small_bytestream\n");
}

static void test_indices_above_u32(void) {
    bytestream bs;
    reasm r;
    assert(bs_init(&bs, 32));
    assert(reasm_init(&r, 32));

    uint64_t base = (1ULL << 32) + 100ULL;
    r.next_index = base;

    assert(reasm_push(&r, &bs, base + 4, (const uint8_t *)"WXYZ", 4, true));
    assert(reasm_pending(&r) == 4);
    assert(reasm_push(&r, &bs, base, (const uint8_t *)"ABCD", 4, false));
    assert(reasm_next_index(&r) == base + 8);
    assert(bs.closed);

    uint8_t out[16];
    size_t n = 0;
    drain_all(&bs, out, &n, sizeof(out));
    assert(n == 8 && memcmp(out, "ABCDWXYZ", 8) == 0);
    assert(bs_finished(&bs));

    reasm_free(&r);
    bs_free(&bs);
    printf("PASS: indices_above_u32\n");
}

int main(void) {
    test_in_order();
    test_reverse_order();
    test_random_100k_shuffle();
    test_beyond_window_drop_and_recover();
    test_empty_fin_and_early_fin();
    test_capacity_one();
    test_flow_control_small_bytestream();
    test_indices_above_u32();
    printf("ALL REASSEMBLER TESTS PASSED\n");
    return 0;
}
