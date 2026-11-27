/* reassembler.h - CP3 TCP stream reassembler (ring buffer + per-byte presence flags).
 * Purpose: reassemble out-of-order, overlapping, or duplicate byte segments into a bytestream.
 * Usage:   reasm_init -> reasm_push* -> reasm_free; query via reasm_pending / reasm_next_index.
 * Status:  written, build not run.
 */
#ifndef SETU_REASSEMBLER_H
#define SETU_REASSEMBLER_H

#include "byte_stream.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    uint8_t *have;
    size_t cap;
    size_t pending;
    uint64_t next_index;
    uint64_t eof_index;
    bool eof_seen;
    bool closed_sent;
} reasm;

/* Initialize a reassembler with the given byte capacity; returns false on invalid args or OOM. */
bool reasm_init(reasm *r, size_t capacity);

/* Release resources owned by *r; idempotent and safe on NULL. */
void reasm_free(reasm *r);

/* Push a segment [index, index+len) (with optional FIN) and flush contiguous prefix to bs. */
bool reasm_push(reasm *r, bytestream *bs, uint64_t index, const uint8_t *data, size_t len, bool last);

/* Return number of out-of-order bytes currently buffered in the reassembler. */
size_t reasm_pending(const reasm *r);

/* Return the 64-bit index of the first unassembled byte. */
uint64_t reasm_next_index(const reasm *r);

#endif /* SETU_REASSEMBLER_H */
