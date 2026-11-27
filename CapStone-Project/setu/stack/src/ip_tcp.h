/* ip_tcp.h - CP3/CP4 IPv4 + TCP header parser, builder, and RFC 1071 checksum.
 * Purpose: parse/validate incoming IPv4+TCP packets and serialize outgoing segments.
 * Usage:   linked into setu_core; used by sender and tcp_connection.
 * Status:  written, build not run.
 */
#ifndef SETU_IP_TCP_H
#define SETU_IP_TCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
    bool syn;
    bool ackf;
    bool fin;
    bool rst;
    bool psh;
    uint16_t window;
    uint16_t mss; /* 0 = absent */
    uint8_t ttl;
    const uint8_t *payload;
    size_t payload_len;
} ip_tcp_seg;

/* RFC 1071 ones-complement checksum over data, seeded with unfolded seed_sum. */
uint16_t inet_checksum(const uint8_t *data, size_t len, uint32_t seed_sum);

/* Parse and validate IPv4+TCP packet; out->payload points into pkt on success. */
bool ip_tcp_parse(const uint8_t *pkt, size_t len, ip_tcp_seg *out);

/* Build IPv4+TCP packet into buf (cap bytes); returns total length or 0 if cap too small. */
size_t ip_tcp_build(uint8_t *buf, size_t cap, const ip_tcp_seg *s, uint16_t ip_id);

#endif /* SETU_IP_TCP_H */
