/* sender.h - CP4 TCP Sender: segmentation, in-flight tracking, RTO, Karn, fast retransmit.
 * Purpose: read bytes from an input bytestream, emit MSS-bounded TCP segments, track
 *          in-flight segments, and handle ACKs, timeouts, and duplicate-ACK fast retransmit.
 * Usage:   sender_init -> sender_start -> loop(sender_poll / sender_on_ack) -> sender_free.
 * Status:  written, build not run.
 */
#ifndef SETU_SENDER_H
#define SETU_SENDER_H

#include "byte_stream.h"
#include "tcp_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t abs_seq;
    size_t len;
    bool syn;
    bool fin;
    uint64_t sent_at_ms;
    bool retransmitted;
    uint8_t *data;
} sender_seg;

typedef struct {
    uint32_t seq;
    bool syn;
    bool fin;
    bool is_retx;
    const uint8_t *payload;
    size_t len;
} sender_out;

typedef struct {
    uint32_t isn;
    uint32_t mss;
    rto_est rto;

    sender_seg *q;
    uint8_t *slab;
    size_t q_cap;
    size_t q_head;
    size_t q_len;

    uint64_t next_abs;
    uint64_t acked_abs;
    uint16_t peer_window;

    bool started;
    bool syn_sent;
    bool fin_sent;

    bool timer_running;
    uint64_t deadline_ms;

    uint32_t dup_acks;
    bool fast_retx_latched;
    bool fast_retx_pending;

    uint64_t n_timeouts;
    uint64_t n_fast_retx;
    uint64_t n_retx_segs;
} sender;

bool sender_init(sender *s, uint32_t isn, uint32_t mss,
                 uint32_t rto_init_ms, uint32_t rto_min_ms, uint32_t rto_max_ms,
                 size_t max_segs);
void sender_free(sender *s);
void sender_start(sender *s);
bool sender_poll(sender *s, bytestream *in, uint64_t now_ms, sender_out *o);
void sender_on_ack(sender *s, uint32_t ackno, uint16_t window, uint64_t now_ms);
uint64_t sender_next_deadline(const sender *s);
bool sender_done(const sender *s);
size_t sender_inflight(const sender *s);

#endif /* SETU_SENDER_H */
