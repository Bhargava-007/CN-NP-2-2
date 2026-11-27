/* ip_tcp.c - IPv4 + TCP header parser, builder, and RFC 1071 checksum.
 * Purpose: validate IPv4/TCP headers and checksums without copying payload, and
 *          serialize outgoing IPv4+TCP packets into a caller-supplied buffer.
 * Usage:   linked into setu_core; see ip_tcp.h for API.
 * Status:  written, build not run.
 */
#include "ip_tcp.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xffu);
}

static void write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xffu);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static uint32_t tcp_pseudo_seed(uint32_t src_ip, uint32_t dst_ip, uint16_t tcp_len) {
    return (src_ip >> 16) + (src_ip & 0xffffu) +
           (dst_ip >> 16) + (dst_ip & 0xffffu) +
           6u + (uint32_t)tcp_len;
}

uint16_t inet_checksum(const uint8_t *data, size_t len, uint32_t seed_sum) {
    uint32_t sum = seed_sum;
    size_t i = 0;
    while (i + 1 < len) {
        sum += ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1];
        i += 2;
    }
    if (i < len) {
        sum += (uint32_t)data[i] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xffffu);
}

bool ip_tcp_parse(const uint8_t *pkt, size_t len, ip_tcp_seg *out) {
    if (!pkt || !out || len < 40u) {
        return false;
    }
    uint8_t ver = (uint8_t)(pkt[0] >> 4);
    uint8_t ihl = (uint8_t)(pkt[0] & 0x0fu);
    if (ver != 4u || ihl < 5u) {
        return false;
    }
    size_t ip_hlen = (size_t)ihl * 4u;
    if (ip_hlen > len) {
        return false;
    }
    uint16_t total_len = read_u16(pkt + 2);
    if (total_len > len || total_len < ip_hlen + 20u) {
        return false;
    }
    if (pkt[9] != 6u) {
        return false;
    }
    /* Reject fragmented packets: MF bit (0x2000) or non-zero fragment offset (0x1fff). */
    uint16_t frag = read_u16(pkt + 6);
    if ((frag & 0x3fffu) != 0u) {
        return false;
    }
    if (inet_checksum(pkt, ip_hlen, 0u) != 0u) {
        return false;
    }

    const uint8_t *tcp = pkt + ip_hlen;
    size_t tcp_total = (size_t)total_len - ip_hlen;
    uint8_t doff = (uint8_t)(tcp[12] >> 4);
    if (doff < 5u) {
        return false;
    }
    size_t tcp_hlen = (size_t)doff * 4u;
    if (tcp_hlen > tcp_total) {
        return false;
    }

    uint32_t src_ip = read_u32(pkt + 12);
    uint32_t dst_ip = read_u32(pkt + 16);
    uint32_t seed = tcp_pseudo_seed(src_ip, dst_ip, (uint16_t)tcp_total);
    if (inet_checksum(tcp, tcp_total, seed) != 0u) {
        return false;
    }

    uint16_t mss = 0;
    size_t opt_pos = 20u;
    while (opt_pos < tcp_hlen) {
        uint8_t kind = tcp[opt_pos];
        if (kind == 0u) {
            break;
        }
        if (kind == 1u) {
            opt_pos++;
            continue;
        }
        if (opt_pos + 1u >= tcp_hlen) {
            return false;
        }
        uint8_t olen = tcp[opt_pos + 1u];
        if (olen < 2u || opt_pos + (size_t)olen > tcp_hlen) {
            return false;
        }
        if (kind == 2u) {
            if (olen != 4u) {
                return false;
            }
            mss = read_u16(tcp + opt_pos + 2u);
        }
        opt_pos += (size_t)olen;
    }

    uint8_t flags = tcp[13];
    memset(out, 0, sizeof(*out));
    out->src_ip = src_ip;
    out->dst_ip = dst_ip;
    out->sport = read_u16(tcp + 0);
    out->dport = read_u16(tcp + 2);
    out->seq = read_u32(tcp + 4);
    out->ack = read_u32(tcp + 8);
    out->fin = (flags & 0x01u) != 0;
    out->syn = (flags & 0x02u) != 0;
    out->rst = (flags & 0x04u) != 0;
    out->psh = (flags & 0x08u) != 0;
    out->ackf = (flags & 0x10u) != 0;
    out->window = read_u16(tcp + 14);
    out->mss = mss;
    out->ttl = pkt[8];
    out->payload_len = tcp_total - tcp_hlen;
    out->payload = (out->payload_len > 0u) ? (tcp + tcp_hlen) : NULL;
    return true;
}

size_t ip_tcp_build(uint8_t *buf, size_t cap, const ip_tcp_seg *s, uint16_t ip_id) {
    if (!buf || !s) {
        return 0;
    }
    if (s->payload_len > 0 && !s->payload) {
        return 0;
    }
    size_t tcp_hlen = (s->syn && s->mss > 0) ? 24u : 20u;
    size_t ip_hlen = 20u;
    if (s->payload_len > 65535u - ip_hlen - tcp_hlen) {
        return 0;
    }
    size_t total = ip_hlen + tcp_hlen + s->payload_len;
    if (total > cap) {
        return 0;
    }

    memset(buf, 0, ip_hlen + tcp_hlen);
    buf[0] = 0x45u;
    buf[1] = 0x00u;
    write_u16(buf + 2, (uint16_t)total);
    write_u16(buf + 4, ip_id);
    write_u16(buf + 6, 0x4000u); /* DF set */
    buf[8] = (s->ttl != 0) ? s->ttl : 64u;
    buf[9] = 6u; /* TCP */
    write_u32(buf + 12, s->src_ip);
    write_u32(buf + 16, s->dst_ip);
    uint16_t ip_sum = inet_checksum(buf, ip_hlen, 0u);
    write_u16(buf + 10, ip_sum);

    uint8_t *tcp = buf + ip_hlen;
    write_u16(tcp + 0, s->sport);
    write_u16(tcp + 2, s->dport);
    write_u32(tcp + 4, s->seq);
    write_u32(tcp + 8, s->ack);
    tcp[12] = (uint8_t)((tcp_hlen / 4u) << 4);
    uint8_t flags = 0;
    if (s->fin) flags |= 0x01u;
    if (s->syn) flags |= 0x02u;
    if (s->rst) flags |= 0x04u;
    if (s->psh) flags |= 0x08u;
    if (s->ackf) flags |= 0x10u;
    tcp[13] = flags;
    write_u16(tcp + 14, s->window);

    if (tcp_hlen == 24u) {
        tcp[20] = 2u;
        tcp[21] = 4u;
        write_u16(tcp + 22, s->mss);
    }
    if (s->payload_len > 0) {
        memcpy(tcp + tcp_hlen, s->payload, s->payload_len);
    }

    size_t tcp_total = tcp_hlen + s->payload_len;
    uint32_t seed = tcp_pseudo_seed(s->src_ip, s->dst_ip, (uint16_t)tcp_total);
    uint16_t tcp_sum = inet_checksum(tcp, tcp_total, seed);
    write_u16(tcp + 16, tcp_sum);
    return total;
}
