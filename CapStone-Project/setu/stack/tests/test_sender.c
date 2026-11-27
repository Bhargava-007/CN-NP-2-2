/* test_sender.c - Unit tests for CP4 TCP Sender.
 * Purpose: verify handshake, segmentation, window, RTO timeout, Karn halves 1 & 2,
 *          fast retransmit, FIN, 32-bit wrap, 1 MB lossy channel with receiver, invalid ACKs.
 * Usage:   ctest --test-dir build -C Debug (or run ./test_sender directly).
 * Status:  written, build not run.
 */
#include "../src/receiver.h"
#include "../src/sender.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_handshake(void) {
    bytestream bs;
    sender s;
    assert(bs_init(&bs, 4096));
    assert(sender_init(&s, 1000, 1000, 200, 100, 60000, 16));

    sender_out o;
    assert(!sender_poll(&s, &bs, 0, &o));

    sender_start(&s);
    assert(sender_poll(&s, &bs, 10, &o));
    assert(o.syn && !o.fin && o.len == 0 && o.seq == 1000);
    assert(!sender_poll(&s, &bs, 10, &o));

    assert(bs_write(&bs, "hello", 5) == 5);
    sender_on_ack(&s, 1001, 1000, 20);
    assert(sender_poll(&s, &bs, 20, &o));
    assert(!o.syn && o.len == 5 && o.seq == 1001 && memcmp(o.payload, "hello", 5) == 0);

    sender_free(&s);
    bs_free(&bs);
    printf("PASS: handshake\n");
}

static void test_segmentation(void) {
    bytestream bs;
    sender s;
    assert(bs_init(&bs, 4096));
    assert(sender_init(&s, 500, 1000, 200, 100, 60000, 16));

    uint8_t data[3500];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i & 0xffu);
    }
    assert(bs_write(&bs, (const char *)data, sizeof(data)) == sizeof(data));

    sender_start(&s);
    sender_out o;
    assert(sender_poll(&s, &bs, 0, &o) && o.syn);
    sender_on_ack(&s, 501, 10000, 1);

    const size_t expected_lens[4] = {1000, 1000, 1000, 500};
    size_t off = 0;
    for (int i = 0; i < 4; i++) {
        assert(sender_poll(&s, &bs, 2, &o));
        assert(o.len == expected_lens[i]);
        assert(o.seq == 501u + (uint32_t)off);
        assert(memcmp(o.payload, data + off, o.len) == 0);
        off += o.len;
    }
    assert(!sender_poll(&s, &bs, 2, &o));

    sender_free(&s);
    bs_free(&bs);
    printf("PASS: segmentation\n");
}

static void test_window_honoured(void) {
    bytestream bs;
    sender s;
    assert(bs_init(&bs, 8192));
    assert(sender_init(&s, 100, 1000, 200, 100, 60000, 16));

    char buf[5000];
    memset(buf, 'A', sizeof(buf));
    assert(bs_write(&bs, buf, sizeof(buf)) == sizeof(buf));

    sender_start(&s);
    sender_out o;
    assert(sender_poll(&s, &bs, 0, &o) && o.syn);
    sender_on_ack(&s, 101, 1500, 10);

    size_t emitted = 0;
    while (sender_poll(&s, &bs, 10, &o)) {
        emitted += o.len;
        assert(sender_inflight(&s) <= 1500);
    }
    assert(emitted == 1500 && sender_inflight(&s) == 1500);

    sender_on_ack(&s, 101 + 1000, 1500, 20);
    while (sender_poll(&s, &bs, 20, &o)) {
        emitted += o.len;
        assert(sender_inflight(&s) <= 1500);
    }
    assert(emitted == 2500);

    sender_free(&s);
    bs_free(&bs);
    printf("PASS: window_honoured\n");
}

static void test_timeout_and_karn(void) {
    /* Timeout doubling + poll just before deadline returns false. */
    bytestream bs;
    sender s;
    assert(bs_init(&bs, 4096));
    assert(sender_init(&s, 10, 1000, 200, 100, 60000, 16));
    assert(bs_write(&bs, "abc", 3) == 3);
    sender_start(&s);

    sender_out o;
    assert(sender_poll(&s, &bs, 0, &o) && o.syn);
    sender_on_ack(&s, 11, 1000, 10);
    uint32_t srtt_syn = s.rto.srtt;
    assert(sender_poll(&s, &bs, 10, &o) && o.len == 3 && !o.is_retx);

    uint64_t dl = sender_next_deadline(&s);
    assert(!sender_poll(&s, &bs, dl - 1, &o));

    uint32_t rto_before = s.rto.rto;
    assert(sender_poll(&s, &bs, dl, &o));
    assert(o.is_retx && o.seq == 11 && o.len == 3);
    assert(s.rto.rto == rto_before * 2u);
    assert(s.n_timeouts == 1);

    /* Karn half 1 (have_sample == false): ACK of retransmitted segment must NOT call rto_sample. */
    sender_on_ack(&s, 14, 1000, dl + 50);
    assert(s.rto.have_sample && s.rto.srtt == srtt_syn);
    assert(s.rto.rto == rto_before * 2u);

    /* Karn half 1 & 2 with an existing sample: 2 timeouts -> rto x4; ack keeps backed-off rto; fresh ack updates it. */
    assert(bs_write(&bs, "def", 3) == 3);
    assert(sender_poll(&s, &bs, 1000, &o) && o.len == 3);
    sender_on_ack(&s, 17, 1000, 1040); /* RTT 40 ms -> have_sample=true */
    assert(s.rto.have_sample);
    uint32_t srtt_saved = s.rto.srtt;

    assert(bs_write(&bs, "ghi", 3) == 3);
    assert(sender_poll(&s, &bs, 2000, &o) && o.len == 3);
    uint32_t rto_pre = s.rto.rto;
    uint64_t d1 = sender_next_deadline(&s);
    assert(sender_poll(&s, &bs, d1, &o) && o.is_retx);
    uint64_t d2 = sender_next_deadline(&s);
    assert(sender_poll(&s, &bs, d2, &o) && o.is_retx);
    assert(s.rto.rto == rto_pre * 4u);

    sender_on_ack(&s, 20, 1000, d2 + 20);
    assert(s.rto.srtt == srtt_saved);
    assert(s.rto.rto == rto_pre * 4u);

    assert(bs_write(&bs, "jkl", 3) == 3);
    assert(sender_poll(&s, &bs, 5000, &o) && o.len == 3 && !o.is_retx);
    sender_on_ack(&s, 23, 1000, 5030);
    assert(s.rto.rto != rto_pre * 4u);

    sender_free(&s);
    bs_free(&bs);
    printf("PASS: timeout_and_karn\n");
}

static void test_fast_retransmit(void) {
    bytestream bs;
    sender s;
    assert(bs_init(&bs, 8192));
    assert(sender_init(&s, 100, 100, 200, 100, 60000, 16));

    char buf[500];
    memset(buf, 'X', sizeof(buf));
    assert(bs_write(&bs, buf, sizeof(buf)) == 500);

    sender_start(&s);
    sender_out o;
    assert(sender_poll(&s, &bs, 0, &o) && o.syn);
    sender_on_ack(&s, 101, 1000, 1);

    for (int i = 0; i < 5; i++) {
        assert(sender_poll(&s, &bs, 1, &o) && o.len == 100);
    }
    sender_on_ack(&s, 201, 1000, 5); /* ack first segment */
    uint32_t rto_saved = s.rto.rto;

    for (int d = 0; d < 3; d++) {
        sender_on_ack(&s, 201, 1000, 6);
    }
    assert(sender_poll(&s, &bs, 6, &o));
    assert(o.is_retx && o.seq == 201 && o.len == 100);
    assert(s.n_fast_retx == 1 && s.rto.rto == rto_saved);

    /* 4th, 5th, 6th dup acks -> no further retransmit. */
    for (int d = 0; d < 3; d++) {
        sender_on_ack(&s, 201, 1000, 7);
    }
    assert(!sender_poll(&s, &bs, 7, &o));
    assert(s.n_fast_retx == 1);

    sender_free(&s);
    bs_free(&bs);
    printf("PASS: fast_retransmit\n");
}

static void test_fin_and_empty_fin(void) {
    /* Empty input closed right after SYN-ACK: FIN seq == isn+1. */
    bytestream bs;
    sender s;
    assert(bs_init(&bs, 64));
    assert(sender_init(&s, 777, 100, 200, 100, 60000, 8));
    sender_start(&s);

    sender_out o;
    assert(sender_poll(&s, &bs, 0, &o) && o.syn);
    sender_on_ack(&s, 778, 1000, 1);
    bs_close(&bs);

    assert(sender_poll(&s, &bs, 1, &o));
    assert(o.fin && !o.syn && o.len == 0 && o.seq == 778);
    assert(!sender_done(&s));

    uint64_t dl = sender_next_deadline(&s);
    assert(sender_poll(&s, &bs, dl, &o) && o.fin && o.is_retx && o.seq == 778);
    assert(!sender_done(&s));

    sender_on_ack(&s, 779, 1000, dl + 5);
    assert(sender_done(&s));

    sender_free(&s);
    bs_free(&bs);
    printf("PASS: fin_and_empty_fin\n");
}

static void test_wrap_1mb(void) {
    const size_t total = 1024u * 1024u;
    uint8_t *src = (uint8_t *)malloc(total);
    assert(src);
    for (size_t i = 0; i < total; i++) {
        src[i] = (uint8_t)(i & 0xffu);
    }

    bytestream bs;
    sender s;
    uint32_t isn = 0xFFFFFF00u;
    assert(bs_init(&bs, 65536));
    assert(sender_init(&s, isn, 1460, 200, 100, 60000, 64));
    sender_start(&s);

    sender_out o;
    assert(sender_poll(&s, &bs, 0, &o) && o.syn && o.seq == isn);
    sender_on_ack(&s, isn + 1u, 32768, 1);

    size_t pushed = 0, acked = 0;
    uint64_t t = 2;
    while (!sender_done(&s)) {
        if (pushed < total && bs_remaining(&bs) > 0) {
            size_t w = bs_write(&bs, (const char *)(src + pushed), total - pushed);
            pushed += w;
            if (pushed == total) {
                bs_close(&bs);
            }
        }
        while (sender_poll(&s, &bs, t, &o)) {
            uint32_t exp_seq = (uint32_t)(isn + 1u + (uint32_t)acked);
            assert(o.seq == exp_seq);
            uint32_t adv = (uint32_t)o.len + (o.fin ? 1u : 0u);
            acked += o.len;
            sender_on_ack(&s, o.seq + adv, 32768, t + 1);
            t += 2;
        }
    }
    assert(acked == total);

    free(src);
    sender_free(&s);
    bs_free(&bs);
    printf("PASS: wrap_1mb\n");
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void test_lossy_1mb_with_receiver(void) {
    const size_t total = 1024u * 1024u;
    uint8_t *src = (uint8_t *)malloc(total);
    uint8_t *dst = (uint8_t *)malloc(total);
    assert(src && dst);

    uint32_t rng = 0xCAFEBABEu;
    for (size_t i = 0; i < total; i++) {
        src[i] = (uint8_t)(xorshift32(&rng) & 0xffu);
    }

    bytestream in_bs, out_bs;
    sender tx;
    receiver rx;
    assert(bs_init(&in_bs, 65536));
    assert(bs_init(&out_bs, 65536));
    assert(sender_init(&tx, 0xFFFFFF80u, 1460, 50, 20, 5000, 64));
    assert(receiver_init(&rx, 65536));
    sender_start(&tx);

    size_t pushed = 0, dst_len = 0;
    uint64_t now_ms = 0;
    int iter = 0;
    const int max_iters = 500000;

    while (!sender_done(&tx) && iter < max_iters) {
        iter++;
        if (pushed < total && bs_remaining(&in_bs) > 0) {
            size_t w = bs_write(&in_bs, (const char *)(src + pushed), total - pushed);
            pushed += w;
            if (pushed == total) {
                bs_close(&in_bs);
            }
        }

        bool did_work = false;
        sender_out o;
        while (sender_poll(&tx, &in_bs, now_ms, &o)) {
            did_work = true;
            /* Drop ~5% of segments deterministically. */
            if ((xorshift32(&rng) % 20u) == 0u) {
                continue;
            }
            receiver_on_segment(&rx, &out_bs, o.seq, o.syn, o.fin, o.payload, o.len);
            while (bs_len(&out_bs) > 0) {
                char tmp[4096];
                size_t got = bs_peek(&out_bs, tmp, sizeof(tmp));
                assert(dst_len + got <= total);
                memcpy(dst + dst_len, tmp, got);
                dst_len += got;
                bs_pop(&out_bs, got);
            }
            uint32_t ackno = 0;
            if (receiver_ackno(&rx, &ackno)) {
                sender_on_ack(&tx, ackno, receiver_window(&rx, &out_bs), now_ms + 1);
            }
        }

        if (!did_work) {
            uint64_t dl = sender_next_deadline(&tx);
            if (dl != UINT64_MAX && dl > now_ms) {
                now_ms = dl;
            } else {
                now_ms += 1;
            }
        } else {
            now_ms += 1;
        }
    }

    assert(iter < max_iters);
    assert(sender_done(&tx));
    assert(dst_len == total);
    assert(memcmp(src, dst, total) == 0);

    free(src);
    free(dst);
    receiver_free(&rx);
    sender_free(&tx);
    bs_free(&in_bs);
    bs_free(&out_bs);
    printf("PASS: lossy_1mb_with_receiver\n");
}

static void test_invalid_acks(void) {
    bytestream bs;
    sender s;
    assert(bs_init(&bs, 1024));
    assert(sender_init(&s, 100, 100, 200, 100, 60000, 8));
    assert(bs_write(&bs, "hello", 5) == 5);
    sender_start(&s);

    sender_out o;
    assert(sender_poll(&s, &bs, 0, &o) && o.syn);
    sender_on_ack(&s, 101, 500, 5);
    assert(sender_poll(&s, &bs, 5, &o) && o.len == 5);

    uint64_t acked_before = s.acked_abs;
    uint16_t win_before = s.peer_window;
    uint32_t dup_before = s.dup_acks;

    sender_on_ack(&s, 100, 123, 6); /* below acked */
    sender_on_ack(&s, 107, 123, 6); /* above sent (106) */
    assert(s.acked_abs == acked_before);
    assert(s.peer_window == win_before);
    assert(s.dup_acks == dup_before);

    sender_free(&s);
    bs_free(&bs);
    printf("PASS: invalid_acks\n");
}

int main(void) {
    test_handshake();
    test_segmentation();
    test_window_honoured();
    test_timeout_and_karn();
    test_fast_retransmit();
    test_fin_and_empty_fin();
    test_wrap_1mb();
    test_lossy_1mb_with_receiver();
    test_invalid_acks();
    printf("ALL SENDER TESTS PASSED\n");
    return 0;
}
