/* CP0 acceptance: 1 MB through a capacity-2 ByteStream, byte-exact. Run: ctest or test_byte_stream.exe */
#include "../src/byte_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const size_t N = 1u << 20;
    char *src = malloc(N), *dst = malloc(N);
    srand(42);
    for (size_t i = 0; i < N; i++) src[i] = (char)(rand() & 0xFF);

    bytestream s;
    assert(bs_init(&s, 2));
    size_t w = 0, r = 0;
    unsigned step = 0;
    while (r < N) {
        size_t want = 1 + (step++ % 3);                       /* offer 1..3 bytes: more than fits */
        if (w < N) w += bs_write(&s, src + w, (w + want <= N) ? want : N - w);
        if (w == N && !s.closed) bs_close(&s);
        char tmp[3];
        size_t got = bs_peek(&s, tmp, 1 + (step % 2));
        memcpy(dst + r, tmp, got);
        bs_pop(&s, got);
        r += got;
        assert(!bs_finished(&s) || r == N);                 /* finished only when closed AND drained */
    }
    assert(bs_finished(&s));
    assert(memcmp(src, dst, N) == 0);
    assert(s.pushed == N && s.popped == N);
    printf("CP0 ByteStream: 1 MB through capacity 2 - PASS (pushed=%llu popped=%llu)\n",
           (unsigned long long)s.pushed, (unsigned long long)s.popped);
    bs_free(&s); free(src); free(dst);
    return 0;
}
