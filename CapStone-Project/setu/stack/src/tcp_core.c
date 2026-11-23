#include "tcp_core.h"

uint32_t tcp_wrap(uint64_t absolute, uint32_t isn) { return (uint32_t)(isn + absolute); }

uint64_t tcp_unwrap(uint32_t seq, uint64_t checkpoint, uint32_t isn) {
    /* offset of seq from the checkpoint's own wrapped value, taken modulo 2^32 */
    uint32_t off = seq - tcp_wrap(checkpoint, isn);
    uint64_t cand = checkpoint + off;                       /* candidate at or after the checkpoint */
    if (off >= (1u << 31) && cand >= (1ull << 32))          /* closer one wrap earlier? */
        cand -= (1ull << 32);
    return cand;
}

static uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
void rto_init(rto_est *e, uint32_t min_ms, uint32_t max_ms) {
    e->have_sample = false; e->srtt = e->rttvar = 0; e->min_ms = min_ms; e->max_ms = max_ms;
    e->rto = clampu(1000, min_ms, max_ms);                  /* RFC 6298 initial RTO: 1 s */
}
void rto_sample(rto_est *e, uint32_t r) {
    if (!e->have_sample) { e->srtt = r; e->rttvar = r / 2; e->have_sample = true; }
    else {
        uint32_t diff = e->srtt > r ? e->srtt - r : r - e->srtt;
        e->rttvar = (3 * e->rttvar + diff) / 4;             /* RTTVAR FIRST, with the OLD srtt */
        e->srtt   = (7 * e->srtt + r) / 8;
    }
    e->rto = clampu(e->srtt + 4 * e->rttvar, e->min_ms, e->max_ms);
}
void rto_backoff(rto_est *e) { uint64_t d = 2ull * e->rto; e->rto = d > e->max_ms ? e->max_ms : (uint32_t)d; }

void reno_init(reno *c, uint32_t mss) {
    c->state = SLOW_START; c->mss = mss; c->cwnd = 2 * mss; c->ssthresh = UINT32_MAX; c->dup_acks = 0;
}
void reno_on_new_ack(reno *c) {
    c->dup_acks = 0;
    if (c->state == FAST_RECOVERY) { c->cwnd = c->ssthresh; c->state = CONGESTION_AVOIDANCE; return; }
    if (c->cwnd < c->ssthresh) { c->cwnd += c->mss; if (c->cwnd >= c->ssthresh) c->state = CONGESTION_AVOIDANCE; }
    else c->cwnd += (uint32_t)((uint64_t)c->mss * c->mss / c->cwnd);   /* about +1 MSS per RTT */
}
bool reno_on_dup_ack(reno *c) {
    if (c->state == FAST_RECOVERY) { c->cwnd += c->mss; return false; }   /* inflate: keep ACK clock */
    if (++c->dup_acks == 3) {
        uint32_t half = c->cwnd / 2;
        c->ssthresh = half > 2 * c->mss ? half : 2 * c->mss;
        c->cwnd = c->ssthresh + 3 * c->mss;
        c->state = FAST_RECOVERY;
        return true;
    }
    return false;
}
void reno_on_timeout(reno *c) {
    uint32_t half = c->cwnd / 2;
    c->ssthresh = half > 2 * c->mss ? half : 2 * c->mss;
    c->cwnd = c->mss; c->state = SLOW_START; c->dup_acks = 0;
}
uint32_t reno_send_window(const reno *c, uint32_t peer_rwnd) { return c->cwnd < peer_rwnd ? c->cwnd : peer_rwnd; }
