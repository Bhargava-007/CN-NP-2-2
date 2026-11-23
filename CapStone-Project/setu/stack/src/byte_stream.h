/* byte_stream.h - CP0: a finite, in-order, flow-controlled byte stream (ring buffer). */
#ifndef SETU_BYTE_STREAM_H
#define SETU_BYTE_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char    *buf;            /* ring buffer of cap bytes, allocated once            */
    size_t   cap, head, len; /* head = index of first unread byte; len = bytes held  */
    uint64_t pushed, popped; /* lifetime counters - 64-bit on purpose (CP3 unwrap)   */
    bool     closed, errored;
} bytestream;

bool   bs_init(bytestream *s, size_t capacity);
void   bs_free(bytestream *s);
/* Writer side */
size_t bs_write(bytestream *s, const char *data, size_t n); /* returns bytes ACCEPTED (may be < n) */
size_t bs_remaining(const bytestream *s);                   /* cap - len: the "window"            */
void   bs_close(bytestream *s);
/* Reader side */
size_t bs_len(const bytestream *s);
size_t bs_peek(const bytestream *s, char *out, size_t n);   /* copies without consuming           */
void   bs_pop(bytestream *s, size_t n);
bool   bs_finished(const bytestream *s);                    /* closed AND fully drained            */
#endif
