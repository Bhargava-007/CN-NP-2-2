/* sender.c - CP4 TCP Sender implementation.
 * Purpose: manage bounded segment queue, single RFC 6298 retransmission timer,
 *          Karn's algorithm, and 3-dup-ACK fast retransmit with fake-clock time.
 * Usage:   linked into setu_core; see sender.h for API.
 * Status:  written, build not run.
 */
#include "sender.h"

#include <stdlib.h>
#include <string.h>

static size_t seg_seq_len(const sender_seg *seg) {
    return (seg->syn ? 1u : 0u) + seg->len + (seg->fin ? 1u : 0u);
}

bool sender_init(sender *s, uint32_t isn, uint32_t mss,
                 uint32_t rto_init_ms, uint32_t rto_min_ms, uint32_t rto_max_ms,
                 size_t max_segs) {
    if (!s) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    if (mss == 0 || max_segs == 0) {
        return false;
    }

    sender_seg *q = (sender_seg *)calloc(max_segs, sizeof(sender_seg));
    uint8_t *slab = (uint8_t *)malloc(max_segs * (size_t)mss);
    if (!q || !slab) {
        free(q);
        free(slab);
        return false;
    }
    for (size_t i = 0; i < max_segs; i++) {
        q[i].data = slab + i * (size_t)mss;
    }

    s->isn = isn;
    s->mss = mss;
    rto_init(&s->rto, rto_min_ms, rto_max_ms);
    s->rto.rto = rto_init_ms;
    s->q = q;
    s->slab = slab;
    s->q_cap = max_segs;
    s->deadline_ms = UINT64_MAX;
    return true;
}

void sender_free(sender *s) {
    if (!s) {
        return;
    }
    free(s->q);
    free(s->slab);
    memset(s, 0, sizeof(*s));
    s->deadline_ms = UINT64_MAX;
}

void sender_start(sender *s) {
    if (s) {
        s->started = true;
    }
}

static void fill_out(const sender *s, const sender_seg *seg, bool is_retx, sender_out *o) {
    o->seq = tcp_wrap(seg->abs_seq, s->isn);
    o->syn = seg->syn;
    o->fin = seg->fin;
    o->is_retx = is_retx;
    o->payload = (seg->len > 0) ? seg->data : NULL;
    o->len = seg->len;
}

bool sender_poll(sender *s, bytestream *in, uint64_t now_ms, sender_out *o) {
    if (!s || !o || !s->started || !s->q) {
        return false;
    }

    /* 1. Retransmission timer expiry. */
    if (s->timer_running && s->q_len > 0 && now_ms >= s->deadline_ms) {
        sender_seg *seg = &s->q[s->q_head];
        seg->retransmitted = true;
        seg->sent_at_ms = now_ms;
        rto_backoff(&s->rto);
        s->deadline_ms = now_ms + (uint64_t)s->rto.rto;
        s->fast_retx_pending = false;
        s->dup_acks = 0;
        s->n_timeouts++;
        s->n_retx_segs++;
        fill_out(s, seg, true, o);
        return true;
    }

    /* 2. Pending fast retransmit. */
    if (s->fast_retx_pending && s->q_len > 0) {
        s->fast_retx_pending = false;
        sender_seg *seg = &s->q[s->q_head];
        seg->retransmitted = true;
        seg->sent_at_ms = now_ms;
        s->n_retx_segs++;
        fill_out(s, seg, true, o);
        return true;
    }

    /* 3. New segments: SYN first, then data up to effective window, then FIN. */
    if (s->q_len >= s->q_cap) {
        return false;
    }

    if (!s->syn_sent) {
        size_t tail = (s->q_head + s->q_len) % s->q_cap;
        sender_seg *seg = &s->q[tail];
        seg->abs_seq = 0;
        seg->len = 0;
        seg->syn = true;
        seg->fin = false;
        seg->sent_at_ms = now_ms;
        seg->retransmitted = false;
        s->q_len++;
        s->syn_sent = true;
        s->next_abs = 1;
        if (!s->timer_running) {
            s->timer_running = true;
            s->deadline_ms = now_ms + (uint64_t)s->rto.rto;
        }
        fill_out(s, seg, false, o);
        return true;
    }

    size_t inflight = sender_inflight(s);
    /* Simplification: when peer_window == 0 and nothing is in flight, use 1-byte zero-window probe. */
    size_t eff_win = (s->peer_window > 0) ? (size_t)s->peer_window : (inflight == 0 ? 1u : 0u);
    if (inflight >= eff_win) {
        return false;
    }
    size_t win_rem = eff_win - inflight;

    if (in && bs_len(in) > 0) {
        size_t avail = bs_len(in);
        size_t take = avail < (size_t)s->mss ? avail : (size_t)s->mss;
        if (take > win_rem) {
            take = win_rem;
        }
        if (take == 0) {
            return false;
        }
        size_t tail = (s->q_head + s->q_len) % s->q_cap;
        sender_seg *seg = &s->q[tail];
        size_t got = bs_peek(in, (char *)seg->data, take);
        bs_pop(in, got);
        seg->abs_seq = s->next_abs;
        seg->len = got;
        seg->syn = false;
        seg->fin = false;
        seg->sent_at_ms = now_ms;
        seg->retransmitted = false;
        s->q_len++;
        s->next_abs += (uint64_t)got;
        if (!s->timer_running) {
            s->timer_running = true;
            s->deadline_ms = now_ms + (uint64_t)s->rto.rto;
        }
        fill_out(s, seg, false, o);
        return true;
    }

    if (!s->fin_sent && in && bs_finished(in)) {
        size_t tail = (s->q_head + s->q_len) % s->q_cap;
        sender_seg *seg = &s->q[tail];
        seg->abs_seq = s->next_abs;
        seg->len = 0;
        seg->syn = false;
        seg->fin = true;
        seg->sent_at_ms = now_ms;
        seg->retransmitted = false;
        s->q_len++;
        s->fin_sent = true;
        s->next_abs += 1;
        if (!s->timer_running) {
            s->timer_running = true;
            s->deadline_ms = now_ms + (uint64_t)s->rto.rto;
        }
        fill_out(s, seg, false, o);
        return true;
    }

    return false;
}

void sender_on_ack(sender *s, uint32_t ackno, uint16_t window, uint64_t now_ms) {
    if (!s || !s->syn_sent) {
        return;
    }
    uint64_t abs_ack = tcp_unwrap(ackno, s->acked_abs, s->isn);
    if (abs_ack < s->acked_abs || abs_ack > s->next_abs) {
        return;
    }

    uint16_t prev_win = s->peer_window;
    s->peer_window = window;

    if (abs_ack > s->acked_abs) {
        bool have_clean = false;
        uint64_t clean_sent_at = 0;

        while (s->q_len > 0) {
            sender_seg *head = &s->q[s->q_head];
            uint64_t seg_end = head->abs_seq + (uint64_t)seg_seq_len(head);
            if (seg_end <= abs_ack) {
                if (!head->retransmitted) {
                    have_clean = true;
                    clean_sent_at = head->sent_at_ms;
                }
                s->q_head = (s->q_head + 1u) % s->q_cap;
                s->q_len--;
            } else {
                break;
            }
        }

        s->acked_abs = abs_ack;
        if (have_clean && now_ms >= clean_sent_at) {
            rto_sample(&s->rto, (uint32_t)(now_ms - clean_sent_at));
        }

        s->dup_acks = 0;
        s->fast_retx_latched = false;
        s->fast_retx_pending = false;

        if (s->q_len > 0) {
            s->timer_running = true;
            s->deadline_ms = now_ms + (uint64_t)s->rto.rto;
        } else {
            s->timer_running = false;
            s->deadline_ms = UINT64_MAX;
        }
    } else if (window == prev_win && s->q_len > 0) {
        s->dup_acks++;
        if (s->dup_acks == 3u && !s->fast_retx_latched) {
            s->fast_retx_latched = true;
            s->fast_retx_pending = true;
            s->n_fast_retx++;
        }
    }
}

uint64_t sender_next_deadline(const sender *s) {
    if (!s || !s->timer_running) {
        return UINT64_MAX;
    }
    return s->deadline_ms;
}

bool sender_done(const sender *s) {
    if (!s) {
        return false;
    }
    return s->fin_sent && s->q_len == 0 && s->acked_abs == s->next_abs;
}

size_t sender_inflight(const sender *s) {
    if (!s || s->next_abs <= s->acked_abs) {
        return 0;
    }
    return (size_t)(s->next_abs - s->acked_abs);
}
