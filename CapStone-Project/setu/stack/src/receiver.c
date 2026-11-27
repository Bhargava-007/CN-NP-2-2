/* receiver.c - CP3 TCP Receiver implementation.
 * Purpose: manage SYN/FIN sequence accounting, 32-bit <-> 64-bit seqno unwrapping,
 *          window bounds checking, and ackno/rwnd calculation.
 * Usage:   linked into setu_core; see receiver.h for API.
 * Status:  written, build not run.
 */
#include "receiver.h"

#include <string.h>

bool receiver_init(receiver *rx, size_t capacity) {
    if (!rx) {
        return false;
    }
    memset(rx, 0, sizeof(*rx));
    return reasm_init(&rx->r, capacity);
}

void receiver_free(receiver *rx) {
    if (!rx) {
        return;
    }
    reasm_free(&rx->r);
    rx->isn = 0;
    rx->syn_seen = false;
}

bool receiver_on_segment(receiver *rx, bytestream *bs, uint32_t seqno,
                         bool syn, bool fin, const uint8_t *payload, size_t len) {
    if (!rx || !bs) {
        return false;
    }
    if (len > 0 && !payload) {
        return false;
    }

    if (!rx->syn_seen) {
        if (!syn) {
            return false;
        }
        rx->isn = seqno;
        rx->syn_seen = true;
    } else if (syn && seqno != rx->isn) {
        return false;
    }

    uint64_t next_idx = reasm_next_index(&rx->r);
    uint64_t checkpoint = next_idx + 1ULL;
    uint64_t abs_seq = tcp_unwrap(seqno, checkpoint, rx->isn);

    if (syn && len == 0 && !fin) {
        return true;
    }

    size_t bs_rem = bs_remaining(bs);
    size_t win_len = rx->r.cap < bs_rem ? rx->r.cap : bs_rem;
    uint64_t win_start = checkpoint;
    uint64_t win_end = win_start + (uint64_t)win_len;

    uint64_t seg_start = syn ? 1ULL : abs_seq;
    uint64_t seg_end = seg_start + (uint64_t)len + (fin ? 1ULL : 0ULL);

    if (seg_end <= win_start) {
        return false;
    }
    if (seg_start >= win_end && !(fin && len == 0 && seg_start == win_start)) {
        return false;
    }

    uint64_t stream_idx = 0;
    const uint8_t *p = payload;
    size_t plen = len;
    if (!syn) {
        if (abs_seq >= 1ULL) {
            stream_idx = abs_seq - 1ULL;
        } else {
            /* Non-SYN segment starting at ISN (abs_seq == 0): skip the SYN slot. */
            if (len == 0) {
                return false;
            }
            stream_idx = 0;
            p = payload + 1;
            plen = len - 1;
        }
    }

    return reasm_push(&rx->r, bs, stream_idx, p, plen, fin);
}

bool receiver_ackno(const receiver *rx, uint32_t *out) {
    if (!rx || !out || !rx->syn_seen) {
        return false;
    }
    uint64_t next_idx = reasm_next_index(&rx->r);
    uint64_t fin_off = (rx->r.eof_seen && next_idx == rx->r.eof_index) ? 1ULL : 0ULL;
    *out = tcp_wrap(next_idx + 1ULL + fin_off, rx->isn);
    return true;
}

uint16_t receiver_window(const receiver *rx, const bytestream *bs) {
    (void)rx;
    size_t rem = bs_remaining(bs);
    return (uint16_t)(rem > 65535u ? 65535u : rem);
}
