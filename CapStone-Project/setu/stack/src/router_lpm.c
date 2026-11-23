#include "router_lpm.h"
uint32_t ip4(int a, int b, int c, int d) { return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d; }

const route *lpm(const route *t, size_t n, uint32_t dst) {
    const route *best = NULL;
    for (size_t i = 0; i < n; i++) {
        uint32_t m = t[i].len ? (0xFFFFFFFFu << (32 - t[i].len)) : 0;   /* /0 special case: shift by 32 is UB */
        if ((dst & m) == (t[i].prefix & m) && (!best || t[i].len > best->len)) best = &t[i];
    }
    return best;                                                      /* NULL = no route = drop */
}

uint16_t ipv4_checksum(const uint8_t *h, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)(h[i] << 8 | h[i + 1]);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int forward_decrement_ttl(uint8_t *ip) {
    if (ip[8] <= 1) return -1;              /* TTL 1 must be dropped, not forwarded with 0 */
    ip[8]--;                                /* decrement BEFORE forwarding */
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4;
    ip[10] = ip[11] = 0;                    /* TTL is inside the checksummed header: recompute */
    uint16_t c = ipv4_checksum(ip, ihl);
    ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)c;
    return 0;
}
