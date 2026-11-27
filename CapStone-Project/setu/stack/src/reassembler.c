/* reassembler.c - CP3 TCP stream reassembler implementation.
 * Purpose: store out-of-order bytes in a fixed ring buffer indexed by 64-bit sequence offset
 *          modulo capacity, and flush contiguous prefixes into bytestream.
 * Usage:   linked into setu_core; see reassembler.h for API.
 * Status:  written, build not run.
 */
#include "reassembler.h"

#include <stdlib.h>
#include <string.h>

static void reasm_zero(reasm *r) {
    r->data = NULL;
    r->have = NULL;
    r->cap = 0;
    r->pending = 0;
    r->next_index = 0;
    r->eof_index = 0;
    r->eof_seen = false;
    r->closed_sent = false;
}

bool reasm_init(reasm *r, size_t capacity) {
    if (!r) {
        return false;
    }
    reasm_zero(r);
    if (capacity == 0) {
        return false;
    }
    uint8_t *data = (uint8_t *)malloc(capacity);
    uint8_t *have = (uint8_t *)calloc(capacity, 1);
    if (!data || !have) {
        free(data);
        free(have);
        return false;
    }
    r->data = data;
    r->have = have;
    r->cap = capacity;
    return true;
}

void reasm_free(reasm *r) {
    if (!r) {
        return;
    }
    free(r->data);
    free(r->have);
    reasm_zero(r);
}

bool reasm_push(reasm *r, bytestream *bs, uint64_t index, const uint8_t *data, size_t len, bool last) {
    if (!r || !bs || !r->data || !r->have || r->cap == 0) {
        return false;
    }
    if (len > 0 && !data) {
        return false;
    }

    if (last && !r->eof_seen) {
        r->eof_index = index + (uint64_t)len;
        r->eof_seen = true;
    }

    uint64_t win_start = r->next_index;
    size_t bs_rem = bs_remaining(bs);
    size_t win_len = r->cap < bs_rem ? r->cap : bs_rem;
    uint64_t win_end = win_start + (uint64_t)win_len;
    uint64_t seg_end = index + (uint64_t)len;

    uint64_t clip_start = (index > win_start) ? index : win_start;
    uint64_t clip_end = (seg_end < win_end) ? seg_end : win_end;

    for (uint64_t pos = clip_start; pos < clip_end; pos++) {
        size_t slot = (size_t)(pos % (uint64_t)r->cap);
        if (!r->have[slot]) {
            r->data[slot] = data[(size_t)(pos - index)];
            r->have[slot] = 1;
            r->pending++;
        }
        /* Keep first-arrived byte on conflict: prevents retransmitted/corrupted overlaps
         * from mutating already-accepted in-window bytes and keeps writes idempotent. */
    }

    while (r->pending > 0) {
        size_t slot = (size_t)(r->next_index % (uint64_t)r->cap);
        if (!r->have[slot]) {
            break;
        }
        size_t run = 0;
        while (slot + run < r->cap && r->have[slot + run] && run < r->pending) {
            run++;
        }
        size_t wrote = bs_write(bs, (const char *)(r->data + slot), run);
        if (wrote == 0) {
            break;
        }
        memset(r->have + slot, 0, wrote);
        r->pending -= wrote;
        r->next_index += (uint64_t)wrote;
        if (wrote < run) {
            break;
        }
    }

    if (r->eof_seen && !r->closed_sent && r->next_index == r->eof_index) {
        bs_close(bs);
        r->closed_sent = true;
    }
    return true;
}

size_t reasm_pending(const reasm *r) {
    if (!r) {
        return 0;
    }
    return r->pending;
}

uint64_t reasm_next_index(const reasm *r) {
    if (!r) {
        return 0;
    }
    return r->next_index;
}
