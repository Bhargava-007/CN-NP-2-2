/* router_lpm.h - CP6b: longest-prefix match and the forwarding contract (TTL + checksum). */
#ifndef SETU_ROUTER_LPM_H
#define SETU_ROUTER_LPM_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t prefix; uint8_t len; uint32_t next_hop; int iface; } route;
const route *lpm(const route *table, size_t n, uint32_t dst);
uint16_t ipv4_checksum(const uint8_t *hdr, size_t len);        /* RFC 1071 one's-complement sum */
int      forward_decrement_ttl(uint8_t *ip_header);            /* 0 = forward, -1 = drop (TTL expired) */
uint32_t ip4(int a, int b, int c, int d);
#endif
