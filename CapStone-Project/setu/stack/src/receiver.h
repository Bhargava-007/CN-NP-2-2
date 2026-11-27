/* receiver.h - CP3 TCP Receiver built on top of Reassembler and tcp_core.
 * Purpose: translate 32-bit wire sequence numbers (SYN/FIN/payload) into 64-bit stream
 *          offsets for reasm_push, and compute ackno and advertised receive window.
 * Usage:   receiver_init -> receiver_on_segment* / receiver_ackno / receiver_window -> receiver_free.
 * Status:  written, build not run.
 */
#ifndef SETU_RECEIVER_H
#define SETU_RECEIVER_H

#include "reassembler.h"
#include "tcp_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    reasm r;
    uint32_t isn;
    bool syn_seen;
} receiver;

/* Initialize receiver with reassembler capacity; returns false on invalid args or OOM. */
bool receiver_init(receiver *rx, size_t capacity);

/* Release resources owned by *rx; idempotent and safe on NULL. */
void receiver_free(receiver *rx);

/* Process an incoming TCP segment; returns false if ignored (before SYN, bad SYN, or outside window). */
bool receiver_on_segment(receiver *rx, bytestream *bs, uint32_t seqno,
                         bool syn, bool fin, const uint8_t *payload, size_t len);

/* Compute the next expected 32-bit wire ackno; returns false before SYN is received. */
bool receiver_ackno(const receiver *rx, uint32_t *out);

/* Return the advertised 16-bit receive window = min(bs_remaining(bs), 65535). */
uint16_t receiver_window(const receiver *rx, const bytestream *bs);

#endif /* SETU_RECEIVER_H */
