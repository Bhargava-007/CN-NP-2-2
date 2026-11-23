#include "../src/crc32.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int main(void) {
    crc32_init();
    assert(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);        /* the standard check value */
    uint8_t f[1504]; srand(7);
    for (int i = 0; i < 1500; i++) f[i] = (uint8_t)rand();
    size_t n = fcs_append(f, 1500);
    assert(fcs_verify(f, n));
    long missed = 0;
    for (int bit = 0; bit < 1500 * 8; bit++) {                              /* every single-bit error */
        f[bit / 8] ^= (uint8_t)(1u << (bit % 8)); if (fcs_verify(f, n)) missed++; f[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    for (int len = 2; len <= 32; len++)                                      /* bursts up to 32 bits */
        for (int start = 0; start + len <= 1500 * 8; start += 997) {
            uint8_t g[1504]; memcpy(g, f, n);
            g[start / 8] ^= (uint8_t)(1u << (start % 8));
            int end = start + len - 1; g[end / 8] ^= (uint8_t)(1u << (end % 8));   /* burst: first and last bit flipped */
            for (int b = start + 1; b < end; b++) if (rand() & 1) g[b / 8] ^= (uint8_t)(1u << (b % 8));
            if (fcs_verify(g, n)) missed++;
        }
    assert(missed == 0);
    clock_t t0 = clock(); volatile uint32_t x = 0;
    for (int i = 0; i < 2000; i++) x ^= crc32(f, 1500);
    double fast = (double)(clock() - t0) / CLOCKS_PER_SEC; t0 = clock();
    for (int i = 0; i < 2000; i++) x ^= crc32_bitwise(f, 1500);
    double slow = (double)(clock() - t0) / CLOCKS_PER_SEC;
    double mb = 2000.0 * 1500 * 8 / 1e6;
    printf("crc32: check value, all 12000 single-bit errors, bursts <= 32 bits - PASS\n");
    printf("speed: table %.0f Mbit/s, bit-by-bit %.0f Mbit/s\n", fast > 0 ? mb / fast : 0, slow > 0 ? mb / slow : 0);
    return 0;
}
