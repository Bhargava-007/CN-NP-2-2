#include "../src/router_lpm.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    route t[] = {                                   /* deliberately unsorted */
        { ip4(10,20,36,0), 23, ip4(10,20,255,3), 2 },
        { 0, 0, ip4(10,20,255,1), 1 },
        { ip4(10,20,0,0), 16, ip4(10,20,255,5), 3 },
        { ip4(10,20,36,10), 32, 0, 4 },
    };
    size_t n = sizeof t / sizeof t[0];
    assert(lpm(t, n, ip4(10,20,36,10))->len == 32);
    assert(lpm(t, n, ip4(10,20,37,99))->len == 23);
    assert(lpm(t, n, ip4(10,20,48,12))->len == 16);
    assert(lpm(t, n, ip4(8,8,8,8))->len == 0);       /* default route still matches */
    uint8_t h[20] = {0x45,0,0,0x54, 0,0,0x40,0, 64,1, 0,0, 10,20,36,87, 10,20,8,12};
    uint16_t c = ipv4_checksum(h, 20); h[10] = (uint8_t)(c >> 8); h[11] = (uint8_t)c;
    assert(ipv4_checksum(h, 20) == 0);               /* valid header sums to zero */
    assert(forward_decrement_ttl(h) == 0 && h[8] == 63 && ipv4_checksum(h, 20) == 0);
    h[8] = 1; assert(forward_decrement_ttl(h) == -1);
    printf("router_lpm: longest-prefix match (unsorted, /0, /32), TTL + checksum - all PASS\n");
    return 0;
}
