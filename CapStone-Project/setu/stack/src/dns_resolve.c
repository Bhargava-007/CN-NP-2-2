/* dns_resolve.c - CP1b: build a DNS query in wire format, send it over UDP, parse the answer
   (including name compression). No resolver library.
   Usage: dns_resolve.exe <name> [type] [server] [port]
     dns_resolve.exe readings.setu.local A 127.0.0.1 5353     (lab DNS published on Windows port 5353)
     dns_resolve.exe example.com MX 1.1.1.1
   Compare with:  nslookup -port=5353 -type=A readings.setu.local 127.0.0.1                    */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { T_A = 1, T_NS = 2, T_CNAME = 5, T_MX = 15, T_TXT = 16, T_AAAA = 28 };
#define MAX_JUMPS 16

static int qtype_of(const char *s) {
    static const struct { const char *n; int t; } map[] = {
        {"A", T_A}, {"NS", T_NS}, {"CNAME", T_CNAME}, {"MX", T_MX}, {"TXT", T_TXT}, {"AAAA", T_AAAA} };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcmp(s, map[i].n)) return map[i].t;
    return T_A;
}

/* "readings.setu.local" -> 8 readings 4 setu 5 local 0 */
static int encode_name(const char *name, unsigned char *out) {
    int n = 0;
    while (*name) {
        const char *dot = strchr(name, '.');
        int len = dot ? (int)(dot - name) : (int)strlen(name);
        if (len == 0 || len > 63) return -1;
        out[n++] = (unsigned char)len;
        memcpy(out + n, name, (size_t)len); n += len;
        name += len; if (*name == '.') name++;
    }
    out[n++] = 0;
    return n;
}

/* Reads a possibly-compressed name at *off; advances *off past it in the ORIGINAL position. */
static int read_name(const unsigned char *m, int mlen, int *off, char *out, int outsz) {
    int pos = *off, jumped = 0, jumps = 0, o = 0;
    for (;;) {
        if (pos >= mlen) return -1;
        unsigned char len = m[pos];
        if ((len & 0xC0) == 0xC0) {                      /* top two bits 11: 14-bit pointer */
            if (pos + 1 >= mlen) return -1;
            int ptr = ((len & 0x3F) << 8) | m[pos + 1];
            if (ptr >= mlen || ++jumps > MAX_JUMPS) return -1;   /* a message can point to itself */
            if (!jumped) *off = pos + 2;
            jumped = 1; pos = ptr; continue;
        }
        if (len == 0) { if (!jumped) *off = pos + 1; break; }
        if (pos + 1 + len > mlen || o + len + 2 > outsz) return -1;
        if (o) out[o++] = '.';
        memcpy(out + o, m + pos + 1, len); o += len;
        pos += 1 + len;
    }
    out[o] = '\0';
    return 0;
}

static unsigned short rd16(const unsigned char *p) { return (unsigned short)((p[0] << 8) | p[1]); }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <name> [A|AAAA|CNAME|MX|NS|TXT] [server] [port]\n", argv[0]); return 2; }
    const char *name = argv[1]; int qt = qtype_of(argc > 2 ? argv[2] : "A");
    const char *server = argc > 3 ? argv[3] : "1.1.1.1"; int port = argc > 4 ? atoi(argv[4]) : 53;
    if (net_init() != 0) return 1;
    srand((unsigned)time(NULL));

    unsigned char q[512]; memset(q, 0, sizeof q);
    unsigned short id = (unsigned short)(rand() & 0xFFFF);
    q[0] = (unsigned char)(id >> 8); q[1] = (unsigned char)id;   /* ID - must match in the reply */
    q[2] = 0x01;                                                 /* RD: recursion desired       */
    q[5] = 1;                                                    /* QDCOUNT = 1                 */
    int n = 12, k = encode_name(name, q + n);
    if (k < 0) { fprintf(stderr, "bad name\n"); return 1; }
    n += k; q[n++] = (unsigned char)(qt >> 8); q[n++] = (unsigned char)qt; q[n++] = 0; q[n++] = 1;   /* QTYPE, QCLASS=IN */

    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, server, &a.sin_addr);
#ifdef _WIN32
    DWORD tmo = 3000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
#else
    struct timeval tv = {3, 0}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    if (sendto(s, (const char *)q, n, 0, (struct sockaddr *)&a, sizeof a) != n) { fprintf(stderr, "sendto failed\n"); return 1; }

    unsigned char m[4096];
    int mlen;
    for (;;) {
        mlen = recv(s, (char *)m, sizeof m, 0);
        if (mlen < 12) { fprintf(stderr, "no reply (timeout?)\n"); return 1; }
        if (rd16(m) == id) break;                                /* wrong ID: discard - possible spoof */
        fprintf(stderr, "discarding reply with wrong ID 0x%04x\n", rd16(m));
    }
    int rcode = m[3] & 0x0F, qd = rd16(m + 4), an = rd16(m + 6);
    printf(";; id=0x%04x rcode=%d answers=%d  TC=%d RA=%d\n", id, rcode, an, (m[2] >> 1) & 1, m[3] >> 7);
    int off = 12; char buf[512];
    for (int i = 0; i < qd; i++) { if (read_name(m, mlen, &off, buf, sizeof buf)) return 1; off += 4; }
    for (int i = 0; i < an; i++) {
        if (read_name(m, mlen, &off, buf, sizeof buf) || off + 10 > mlen) { fprintf(stderr, "malformed\n"); return 1; }
        int type = rd16(m + off), rdlen = rd16(m + off + 8); unsigned ttl = ((unsigned)rd16(m + off + 4) << 16) | rd16(m + off + 6);
        off += 10;
        if (off + rdlen > mlen) return 1;
        printf("%-30s %6u  ", buf, ttl);
        char t[512]; int r = off;
        switch (type) {
        case T_A:    printf("A      %d.%d.%d.%d\n", m[r], m[r+1], m[r+2], m[r+3]); break;
        case T_AAAA: inet_ntop(AF_INET6, m + r, t, sizeof t); printf("AAAA   %s\n", t); break;
        case T_CNAME: case T_NS: read_name(m, mlen, &r, t, sizeof t); printf("%-6s %s\n", type == T_NS ? "NS" : "CNAME", t); break;
        case T_MX:   r += 2; read_name(m, mlen, &r, t, sizeof t); printf("MX     %d %s\n", rd16(m + off), t); break;
        case T_TXT:  printf("TXT    \"%.*s\"\n", m[r], (const char *)m + r + 1); break;
        default:     printf("type %d (%d bytes)\n", type, rdlen);
        }
        off += rdlen;
    }
    sock_close(s); net_cleanup();
    return 0;
}
