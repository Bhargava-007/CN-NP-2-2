#include "crc32.h"
static uint32_t table[256];
void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;                                  /* one byte of polynomial division, precomputed */
    }
}
static uint32_t crc32_raw(const uint8_t *b, size_t n) {   /* no final XOR */
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *b++) & 0xFF] ^ (c >> 8);
    return c;
}
uint32_t crc32(const uint8_t *b, size_t n) { return crc32_raw(b, n) ^ 0xFFFFFFFFu; }
uint32_t crc32_bitwise(const uint8_t *b, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *b++; for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
    return c ^ 0xFFFFFFFFu;
}
size_t fcs_append(uint8_t *f, size_t n) {
    uint32_t c = crc32(f, n);
    for (int i = 0; i < 4; i++) f[n + i] = (uint8_t)(c >> (8 * i));   /* least-significant byte first */
    return n + 4;
}
/* Raw CRC over frame+FCS lands on one fixed value; with this file's conventions (no final XOR) it is 0xDEBB20E3,
   which is the bit-reflected, final-XORed form of the residue usually quoted as 0x2144DF1C. */
bool fcs_verify(const uint8_t *f, size_t n) { return crc32_raw(f, n) == 0xDEBB20E3u; }
