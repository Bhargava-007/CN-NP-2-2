#ifdef _WIN32
#define _CRT_RAND_S
#endif

#include "platform.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DNS_MAX_UDP  512
#define DNS_MAX_MSG  65535
#define DNS_TIMEOUT_MS 3000

#define TYPE_A     1
#define TYPE_NS    2
#define TYPE_CNAME 5
#define TYPE_SOA   6
#define TYPE_PTR   12
#define TYPE_MX    15
#define TYPE_TXT   16
#define TYPE_AAAA  28

static int ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

/* Why: use OS CSPRNG rather than predictable rand() to prevent off-path ID guessing. */
static int os_random_u16(uint16_t *out) {
#ifdef _WIN32
    unsigned int v = 0;
    if (rand_s(&v) != 0) {
        return -1;
    }
    *out = (uint16_t)(v & 0xffffu);
    return 0;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        return -1;
    }
    size_t n = fread(out, 1, sizeof(*out), f);
    fclose(f);
    return (n == sizeof(*out)) ? 0 : -1;
#endif
}

static int parse_qtype(const char *s, uint16_t *out) {
    if (ci_eq(s, "A")) { *out = TYPE_A; return 0; }
    if (ci_eq(s, "NS")) { *out = TYPE_NS; return 0; }
    if (ci_eq(s, "CNAME")) { *out = TYPE_CNAME; return 0; }
    if (ci_eq(s, "SOA")) { *out = TYPE_SOA; return 0; }
    if (ci_eq(s, "PTR")) { *out = TYPE_PTR; return 0; }
    if (ci_eq(s, "MX")) { *out = TYPE_MX; return 0; }
    if (ci_eq(s, "TXT")) { *out = TYPE_TXT; return 0; }
    if (ci_eq(s, "AAAA")) { *out = TYPE_AAAA; return 0; }
    return -1;
}

static const char *type_name(uint16_t t) {
    switch (t) {
        case TYPE_A: return "A";
        case TYPE_NS: return "NS";
        case TYPE_CNAME: return "CNAME";
        case TYPE_SOA: return "SOA";
        case TYPE_PTR: return "PTR";
        case TYPE_MX: return "MX";
        case TYPE_TXT: return "TXT";
        case TYPE_AAAA: return "AAAA";
        default: return "UNKNOWN";
    }
}

static const char *rcode_name(uint8_t rcode) {
    switch (rcode) {
        case 0: return "NOERROR";
        case 1: return "FORMERR";
        case 2: return "SERVFAIL";
        case 3: return "NXDOMAIN";
        case 4: return "NOTIMP";
        case 5: return "REFUSED";
        default: return "OTHER";
    }
}

/* Why: encode DNS wire name with strict label <= 63, total <= 255, no empty labels. */
static int encode_name(const char *name, uint8_t *out, size_t cap, size_t *out_len) {
    size_t slen = strlen(name);
    if (slen == 0) {
        return -1;
    }
    if (strcmp(name, ".") == 0) {
        if (cap < 1) {
            return -1;
        }
        out[0] = 0;
        *out_len = 1;
        return 0;
    }
    if (name[slen - 1] == '.') {
        slen--;
    }
    if (slen == 0) {
        return -1;
    }

    size_t pos = 0;
    size_t i = 0;
    while (i < slen) {
        size_t start = i;
        while (i < slen && name[i] != '.') {
            i++;
        }
        size_t llen = i - start;
        if (llen == 0 || llen > 63) {
            return -1;
        }
        if (pos + 1 + llen + 1 > cap || pos + 1 + llen + 1 > 255) {
            return -1;
        }
        out[pos++] = (uint8_t)llen;
        memcpy(out + pos, name + start, llen);
        pos += llen;
        if (i < slen && name[i] == '.') {
            i++;
            if (i == slen) {
                return -1;
            }
        }
    }
    out[pos++] = 0;
    *out_len = pos;
    return 0;
}

/* Why: decode compressed DNS names with backward-only pointer and jump-limit protection. */
static int decode_name(const uint8_t *msg, size_t msg_len, size_t offset,
                       char *out, size_t out_cap, size_t *next_offset) {
    size_t cur = offset;
    size_t out_pos = 0;
    int jumped = 0;
    int jumps = 0;
    size_t saved_next = 0;

    if (out_cap == 0) {
        return -1;
    }

    for (;;) {
        if (cur >= msg_len) {
            return -1;
        }
        uint8_t len = msg[cur];
        if (len == 0) {
            if (!jumped) {
                saved_next = cur + 1;
            }
            break;
        }
        if ((len & 0xc0u) == 0xc0u) {
            if (cur + 1 >= msg_len) {
                return -1;
            }
            uint16_t ptr = (uint16_t)(((uint16_t)(len & 0x3fu) << 8) | msg[cur + 1]);
            /* Why: require pointers to point strictly backwards to prevent compression loops. */
            if (ptr >= cur || ptr >= msg_len) {
                return -1;
            }
            if (++jumps > 32) {
                return -1;
            }
            if (!jumped) {
                saved_next = cur + 2;
                jumped = 1;
            }
            cur = ptr;
        } else if ((len & 0xc0u) == 0) {
            cur++;
            if (cur + len > msg_len) {
                return -1;
            }
            if (out_pos > 0) {
                if (out_pos + 1 >= out_cap) {
                    return -1;
                }
                out[out_pos++] = '.';
            }
            if (out_pos + len >= out_cap) {
                return -1;
            }
            memcpy(out + out_pos, msg + cur, len);
            out_pos += len;
            cur += len;
        } else {
            return -1;
        }
    }

    if (out_pos == 0) {
        if (out_cap < 2) {
            return -1;
        }
        out[0] = '.';
        out[1] = '\0';
    } else {
        out[out_pos] = '\0';
    }
    if (next_offset) {
        *next_offset = saved_next;
    }
    return 0;
}

static int sockaddr_eq(const struct sockaddr *a, sock_len_t alen,
                       const struct sockaddr *b, sock_len_t blen) {
    if (a->sa_family != b->sa_family || alen != blen) {
        return 0;
    }
    if (a->sa_family == AF_INET) {
        const struct sockaddr_in *ia = (const struct sockaddr_in *)a;
        const struct sockaddr_in *ib = (const struct sockaddr_in *)b;
        return ia->sin_port == ib->sin_port &&
               ia->sin_addr.s_addr == ib->sin_addr.s_addr;
    }
    if (a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *ia = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *ib = (const struct sockaddr_in6 *)b;
        return ia->sin6_port == ib->sin6_port &&
               memcmp(&ia->sin6_addr, &ib->sin6_addr, 16) == 0;
    }
    return 0;
}

static void set_recv_timeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

static int tcp_query(const struct addrinfo *ai, const uint8_t *q, size_t qlen,
                     uint8_t *resp, size_t resp_cap, size_t *resp_len) {
    sock_t s = socket(ai->ai_family, SOCK_STREAM, 0);
    if (s == SETU_BAD_SOCK) {
        return -1;
    }
    set_recv_timeout(s, DNS_TIMEOUT_MS);
    if (connect(s, ai->ai_addr, (sock_len_t)ai->ai_addrlen) != 0) {
        sock_close(s);
        return -1;
    }

    uint8_t prefix[2];
    put_u16(prefix, (uint16_t)qlen);
    size_t sent = 0;
    while (sent < 2) {
        int n = (int)send(s, (const char *)(prefix + sent), (int)(2 - sent), 0);
        if (n <= 0) { sock_close(s); return -1; }
        sent += (size_t)n;
    }
    sent = 0;
    while (sent < qlen) {
        int n = (int)send(s, (const char *)(q + sent), (int)(qlen - sent), 0);
        if (n <= 0) { sock_close(s); return -1; }
        sent += (size_t)n;
    }

    uint8_t rlen_buf[2];
    size_t got = 0;
    while (got < 2) {
        int n = (int)recv(s, (char *)(rlen_buf + got), (int)(2 - got), 0);
        if (n <= 0) { sock_close(s); return -1; }
        got += (size_t)n;
    }
    uint16_t mlen = get_u16(rlen_buf);
    if (mlen > resp_cap) {
        sock_close(s);
        return -1;
    }
    got = 0;
    while (got < mlen) {
        int n = (int)recv(s, (char *)(resp + got), (int)(mlen - got), 0);
        if (n <= 0) { sock_close(s); return -1; }
        got += (size_t)n;
    }
    sock_close(s);
    *resp_len = mlen;
    return 0;
}

static int print_rr_section(const char *title, const uint8_t *msg, size_t msg_len,
                            size_t *pos, uint16_t count) {
    printf(";; %s (%u):\n", title, count);
    for (uint16_t i = 0; i < count; i++) {
        char owner[256];
        size_t next = 0;
        if (decode_name(msg, msg_len, *pos, owner, sizeof(owner), &next) != 0) {
            return -1;
        }
        if (next + 10 > msg_len) {
            return -1;
        }
        uint16_t rtype = get_u16(msg + next);
        uint32_t ttl = get_u32(msg + next + 4);
        uint16_t rdlen = get_u16(msg + next + 8);
        size_t rdata = next + 10;
        if (rdata + rdlen > msg_len) {
            return -1;
        }

        printf("%s\t%u\t%s\t", owner, ttl, type_name(rtype));
        if (rtype == TYPE_A) {
            if (rdlen != 4) { return -1; }
            printf("%u.%u.%u.%u", msg[rdata], msg[rdata + 1], msg[rdata + 2], msg[rdata + 3]);
        } else if (rtype == TYPE_AAAA) {
            if (rdlen != 16) { return -1; }
            char ip6[INET6_ADDRSTRLEN];
            if (!inet_ntop(AF_INET6, msg + rdata, ip6, sizeof(ip6))) {
                return -1;
            }
            printf("%s", ip6);
        } else if (rtype == TYPE_NS || rtype == TYPE_CNAME || rtype == TYPE_PTR) {
            char target[256];
            size_t end_off = 0;
            if (decode_name(msg, rdata + rdlen, rdata, target, sizeof(target), &end_off) != 0) {
                return -1;
            }
            printf("%s", target);
        } else if (rtype == TYPE_MX) {
            if (rdlen < 3) { return -1; }
            uint16_t pref = get_u16(msg + rdata);
            char exch[256];
            size_t end_off = 0;
            if (decode_name(msg, rdata + rdlen, rdata + 2, exch, sizeof(exch), &end_off) != 0) {
                return -1;
            }
            printf("%u %s", pref, exch);
        } else if (rtype == TYPE_TXT) {
            size_t p = 0;
            int first = 1;
            while (p < rdlen) {
                uint8_t slen = msg[rdata + p];
                p++;
                if (p + slen > rdlen) { return -1; }
                if (!first) { putchar(' '); }
                first = 0;
                putchar('"');
                for (uint8_t k = 0; k < slen; k++) {
                    putchar((char)msg[rdata + p + k]);
                }
                putchar('"');
                p += slen;
            }
        } else if (rtype == TYPE_SOA) {
            char mname[256], rname[256];
            size_t p1 = 0, p2 = 0;
            if (decode_name(msg, rdata + rdlen, rdata, mname, sizeof(mname), &p1) != 0) {
                return -1;
            }
            if (decode_name(msg, rdata + rdlen, p1, rname, sizeof(rname), &p2) != 0) {
                return -1;
            }
            if (p2 + 20 > rdata + rdlen) { return -1; }
            printf("%s %s %u %u %u %u %u", mname, rname,
                   get_u32(msg + p2), get_u32(msg + p2 + 4),
                   get_u32(msg + p2 + 8), get_u32(msg + p2 + 12),
                   get_u32(msg + p2 + 16));
        } else {
            printf("<%u bytes>", rdlen);
        }
        putchar('\n');
        *pos = rdata + rdlen;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 5) {
        fprintf(stderr, "Usage: %s <name> [type] [server] [port]\n",
                argc > 0 ? argv[0] : "dns_resolve");
        return 1;
    }

    const char *raw_name = argv[1];
    const char *type_str = (argc >= 3) ? argv[2] : "A";
    const char *server = (argc >= 4) ? argv[3] : "127.0.0.1";
    const char *port = (argc >= 5) ? argv[4] : "53";

    uint16_t qtype = 0;
    if (parse_qtype(type_str, &qtype) != 0) {
        fprintf(stderr, "dns_resolve: unknown type '%s'\n", type_str);
        return 1;
    }

    char qname_buf[256];
    const char *qname = raw_name;
    if (qtype == TYPE_PTR) {
        unsigned int a, b, c, d;
        char extra;
        if (sscanf(raw_name, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) == 4 &&
            a <= 255 && b <= 255 && c <= 255 && d <= 255) {
            snprintf(qname_buf, sizeof(qname_buf), "%u.%u.%u.%u.in-addr.arpa", d, c, b, a);
            qname = qname_buf;
        }
    }

    uint16_t qid = 0;
    if (os_random_u16(&qid) != 0) {
        fprintf(stderr, "dns_resolve: failed to obtain random query ID\n");
        return 1;
    }

    uint8_t query[DNS_MAX_UDP];
    memset(query, 0, sizeof(query));
    put_u16(query + 0, qid);
    put_u16(query + 2, 0x0100u); /* RD=1 */
    put_u16(query + 4, 1);       /* QDCOUNT=1 */

    size_t qname_len = 0;
    if (encode_name(qname, query + 12, sizeof(query) - 16, &qname_len) != 0) {
        fprintf(stderr, "dns_resolve: invalid domain name '%s'\n", qname);
        return 1;
    }
    size_t qlen = 12 + qname_len;
    put_u16(query + qlen, qtype);
    put_u16(query + qlen + 2, 1); /* IN */
    qlen += 4;

    if (net_init() != 0) {
        fprintf(stderr, "dns_resolve: net_init failed\n");
        return 1;
    }

    int rc = 1;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(server, port, &hints, &res) != 0 || !res) {
        fprintf(stderr, "dns_resolve: getaddrinfo failed for %s:%s\n", server, port);
        net_cleanup();
        return 1;
    }

    sock_t sock = socket(res->ai_family, SOCK_DGRAM, res->ai_protocol);
    if (sock == SETU_BAD_SOCK) {
        freeaddrinfo(res);
        net_cleanup();
        return 1;
    }
    set_recv_timeout(sock, DNS_TIMEOUT_MS);

    uint8_t resp[DNS_MAX_MSG];
    size_t resp_len = 0;
    int got_reply = 0;

    for (int attempt = 0; attempt < 2 && !got_reply; attempt++) {
        if (sendto(sock, (const char *)query, (int)qlen, 0,
                   res->ai_addr, (sock_len_t)res->ai_addrlen) < 0) {
            continue;
        }
        for (;;) {
            struct sockaddr_storage from;
            sock_len_t flen = (sock_len_t)sizeof(from);
            int n = (int)recvfrom(sock, (char *)resp, DNS_MAX_UDP, 0,
                                  (struct sockaddr *)&from, &flen);
            if (n < 0) {
                break; /* timeout or error -> retry */
            }
            if (!sockaddr_eq((const struct sockaddr *)&from, flen,
                             res->ai_addr, (sock_len_t)res->ai_addrlen)) {
                continue;
            }
            if ((size_t)n < 12 || get_u16(resp) != qid) {
                continue;
            }
            uint16_t flags = get_u16(resp + 2);
            if ((flags & 0x8000u) == 0) {
                continue; /* QR must be 1 */
            }
            if (get_u16(resp + 4) != 1 || (size_t)n < qlen ||
                memcmp(resp + 12, query + 12, qname_len + 4) != 0) {
                continue; /* Question must echo our question */
            }
            resp_len = (size_t)n;
            got_reply = 1;
            break;
        }
    }
    sock_close(sock);

    if (!got_reply) {
        fprintf(stderr, "dns_resolve: timeout waiting for reply from %s:%s\n", server, port);
        freeaddrinfo(res);
        net_cleanup();
        return 1;
    }

    uint16_t flags = get_u16(resp + 2);
    if (flags & 0x0200u) {
        printf(";; TC=1 (truncated), retrying over TCP...\n");
        if (tcp_query(res, query, qlen, resp, sizeof(resp), &resp_len) != 0 || resp_len < 12) {
            fprintf(stderr, "dns_resolve: TCP retry failed\n");
            freeaddrinfo(res);
            net_cleanup();
            return 1;
        }
        flags = get_u16(resp + 2);
    }
    freeaddrinfo(res);

    uint8_t rcode = (uint8_t)(flags & 0x0fu);
    uint16_t qdcount = get_u16(resp + 4);
    uint16_t ancount = get_u16(resp + 6);
    uint16_t nscount = get_u16(resp + 8);
    uint16_t arcount = get_u16(resp + 10);

    printf(";; id=%u rcode=%s flags=%s%s%s%s\n",
           get_u16(resp), rcode_name(rcode),
           (flags & 0x0400u) ? "AA " : "",
           (flags & 0x0200u) ? "TC " : "",
           (flags & 0x0100u) ? "RD " : "",
           (flags & 0x0080u) ? "RA" : "");

    size_t pos = 12;
    for (uint16_t i = 0; i < qdcount; i++) {
        char tmp[256];
        size_t next = 0;
        if (decode_name(resp, resp_len, pos, tmp, sizeof(tmp), &next) != 0 ||
            next + 4 > resp_len) {
            fprintf(stderr, "dns_resolve: malformed question\n");
            net_cleanup();
            return 1;
        }
        pos = next + 4;
    }

    if (print_rr_section("ANSWER", resp, resp_len, &pos, ancount) != 0 ||
        print_rr_section("AUTHORITY", resp, resp_len, &pos, nscount) != 0 ||
        print_rr_section("ADDITIONAL", resp, resp_len, &pos, arcount) != 0) {
        fprintf(stderr, "dns_resolve: malformed resource record\n");
        net_cleanup();
        return 1;
    }

    if (rcode == 3 || (rcode == 0 && ancount == 0)) {
        rc = 3;
    } else if (rcode == 0 && ancount > 0) {
        rc = 0;
    } else {
        rc = 1;
    }

    net_cleanup();
    return rc;
}
