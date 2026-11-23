/* byte_stream.c - CP0 reference implementation. Never allocates after bs_init. */
#include "byte_stream.h"
#include <stdlib.h>
#include <string.h>

bool bs_init(bytestream *s, size_t capacity) {
    memset(s, 0, sizeof *s);
    s->buf = (char *)malloc(capacity ? capacity : 1);
    s->cap = capacity;
    return s->buf != NULL;
}
void bs_free(bytestream *s) { free(s->buf); s->buf = NULL; }

size_t bs_remaining(const bytestream *s) { return s->cap - s->len; }
size_t bs_len(const bytestream *s) { return s->len; }
void   bs_close(bytestream *s) { s->closed = true; }
bool   bs_finished(const bytestream *s) { return s->closed && s->len == 0; }

size_t bs_write(bytestream *s, const char *data, size_t n) {
    if (s->closed || s->errored) return 0;
    size_t take = n < bs_remaining(s) ? n : bs_remaining(s);  /* flow control: accept only what fits */
    size_t tail = (s->head + s->len) % (s->cap ? s->cap : 1);
    size_t first = take < s->cap - tail ? take : s->cap - tail;  /* up to the end of the ring */
    memcpy(s->buf + tail, data, first);
    memcpy(s->buf, data + first, take - first);                 /* wrap around */
    s->len += take;
    s->pushed += take;
    return take;
}

size_t bs_peek(const bytestream *s, char *out, size_t n) {
    size_t take = n < s->len ? n : s->len;
    size_t first = take < s->cap - s->head ? take : s->cap - s->head;
    memcpy(out, s->buf + s->head, first);
    memcpy(out + first, s->buf, take - first);
    return take;
}

void bs_pop(bytestream *s, size_t n) {
    if (n > s->len) n = s->len;
    s->head = s->cap ? (s->head + n) % s->cap : 0;
    s->len -= n;
    s->popped += n;
}
