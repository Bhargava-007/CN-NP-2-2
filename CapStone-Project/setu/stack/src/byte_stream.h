#ifndef SETU_BYTE_STREAM_H
#define SETU_BYTE_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *buf;
    size_t cap;
    size_t head;
    size_t len;
    uint64_t pushed;
    uint64_t popped;
    bool closed;
    bool errored;
} bytestream;

/*
 * Initialize a ring-buffer bytestream with the given capacity.
 * Contract: allocates buf once; returns false on NULL, capacity == 0, or OOM,
 * leaving *s zeroed. Invariants: cap > 0, head == 0, len == 0, pushed == 0,
 * popped == 0, closed == false, errored == false.
 */
bool bs_init(bytestream *s, size_t capacity);

/*
 * Release resources owned by *s.
 * Contract: idempotent and safe on NULL or already-freed streams; resets
 * fields to a zeroed state.
 */
void bs_free(bytestream *s);

/*
 * Write up to n bytes from data into *s.
 * Contract: returns accepted = min(n, bs_remaining(s)); writes after close or
 * with NULL args accept 0. Invariants: len <= cap, head < cap,
 * pushed - popped == len.
 */
size_t bs_write(bytestream *s, const char *data, size_t n);

/*
 * Return remaining buffer capacity (cap - len), independent of closed.
 * Contract: returns 0 only if s is NULL, buf is NULL, or len >= cap.
 */
size_t bs_remaining(const bytestream *s);

/*
 * Mark *s as closed to further writes.
 * Contract: safe on NULL; does not discard buffered bytes (closed != finished).
 */
void bs_close(bytestream *s);

/*
 * Return number of buffered bytes currently available to read.
 * Contract: returns 0 if s is NULL. Invariant: len <= cap.
 */
size_t bs_len(const bytestream *s);

/*
 * Copy up to n buffered bytes into out without consuming them.
 * Contract: returns min(n, len), or 0 on NULL args / n == 0.
 * Invariant: head, len, and popped are unchanged.
 */
size_t bs_peek(const bytestream *s, char *out, size_t n);

/*
 * Consume up to n buffered bytes from *s.
 * Contract: pops k = min(n, len), advances head modulo cap, adds k to popped.
 * Safe on NULL or n == 0. Invariants: head < cap, pushed - popped == len.
 */
void bs_pop(bytestream *s, size_t n);

/*
 * Return true iff *s is closed and all buffered bytes have been popped.
 * Contract: returns false if s is NULL.
 */
bool bs_finished(const bytestream *s);

#endif /* SETU_BYTE_STREAM_H */
