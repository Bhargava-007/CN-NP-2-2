#include "byte_stream.h"

#include <stdlib.h>
#include <string.h>

static void bs_zero(bytestream *s) {
    s->buf = NULL;
    s->cap = 0;
    s->head = 0;
    s->len = 0;
    s->pushed = 0;
    s->popped = 0;
    s->closed = false;
    s->errored = false;
}

bool bs_init(bytestream *s, size_t capacity) {
    if (!s) {
        return false;
    }
    bs_zero(s);
    if (capacity == 0) {
        return false;
    }
    char *buf = (char *)malloc(capacity);
    if (!buf) {
        return false;
    }
    s->buf = buf;
    s->cap = capacity;
    return true;
}

void bs_free(bytestream *s) {
    if (!s) {
        return;
    }
    free(s->buf);
    bs_zero(s);
}

size_t bs_remaining(const bytestream *s) {
    if (!s || !s->buf || s->len >= s->cap) {
        return 0;
    }
    return s->cap - s->len;
}

size_t bs_write(bytestream *s, const char *data, size_t n) {
    if (!s || !s->buf || !data || n == 0 || s->closed || s->cap == 0) {
        return 0;
    }
    size_t rem = s->cap - s->len;
    size_t accept = n < rem ? n : rem;
    if (accept == 0) {
        return 0;
    }

    size_t tail = (s->head + s->len) % s->cap;
    size_t first = s->cap - tail;
    if (first > accept) {
        first = accept;
    }
    memcpy(s->buf + tail, data, first);

    size_t second = accept - first;
    if (second > 0) {
        memcpy(s->buf, data + first, second);
    }

    s->len += accept;
    s->pushed += (uint64_t)accept;
    return accept;
}

void bs_close(bytestream *s) {
    if (!s) {
        return;
    }
    s->closed = true;
}

size_t bs_len(const bytestream *s) {
    if (!s) {
        return 0;
    }
    return s->len;
}

size_t bs_peek(const bytestream *s, char *out, size_t n) {
    if (!s || !s->buf || !out || n == 0 || s->len == 0 || s->cap == 0) {
        return 0;
    }
    size_t take = n < s->len ? n : s->len;
    size_t first = s->cap - s->head;
    if (first > take) {
        first = take;
    }
    memcpy(out, s->buf + s->head, first);

    size_t second = take - first;
    if (second > 0) {
        memcpy(out + first, s->buf, second);
    }
    return take;
}

void bs_pop(bytestream *s, size_t n) {
    if (!s || n == 0 || s->len == 0 || s->cap == 0) {
        return;
    }
    size_t drop = n < s->len ? n : s->len;
    s->head = (s->head + drop) % s->cap;
    s->len -= drop;
    s->popped += (uint64_t)drop;
}

bool bs_finished(const bytestream *s) {
    if (!s) {
        return false;
    }
    return s->closed && s->len == 0;
}
