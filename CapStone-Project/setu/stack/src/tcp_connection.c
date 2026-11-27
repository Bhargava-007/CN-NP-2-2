/* tcp_connection.c - CP3/CP4 TCP connection state machine implementation.
 * Purpose: handle handshake, RST, MSS clamping, ACK/data piggybacking, and pure ACKs.
 *          Simplification: no TIME_WAIT linger after FIN exchange completes.
 * Usage:   linked into setu_core; see tcp_connection.h for API.
 * Status:  written, build not run.
 */
#include "tcp_connection.h"

#include <string.h>

#define CONN_BS_CAP 65536u

bool tcp_conn_init(tcp_conn *c, uint32_t lip, uint16_t lport,
                   uint32_t rip, uint16_t rport, uint32_t isn, uint32_t mss) {
    if (!c) {
        return false;
    }
    memset(c, 0, sizeof(*c));
    c->lip = lip;
    c->lport = lport;
    c->rip = rip;
    c->rport = rport;
    c->mss = (mss == 0) ? 1460u : mss;

    if (!bs_init(&c->in, CONN_BS_CAP) ||
        !bs_init(&c->out, CONN_BS_CAP) ||
        !sender_init(&c->tx, isn, c->mss, 1000u, 200u, 60000u, 64u) ||
        !receiver_init(&c->rx, CONN_BS_CAP)) {
        tcp_conn_free(c);
        return false;
    }
    return true;
}

void tcp_conn_free(tcp_conn *c) {
    if (!c) {
        return;
    }
    bs_free(&c->in);
    bs_free(&c->out);
    sender_free(&c->tx);
    receiver_free(&c->rx);
    memset(c, 0, sizeof(*c));
}

void tcp_conn_connect(tcp_conn *c) {
    if (!c) {
        return;
    }
    c->listening = false;
    c->active = true;
    sender_start(&c->tx);
}

void tcp_conn_listen(tcp_conn *c) {
    if (!c) {
        return;
    }
    c->listening = true;
    c->active = false;
}

void tcp_conn_on_packet(tcp_conn *c, const uint8_t *pkt, size_t len, uint64_t now_ms) {
    if (!c || c->rst) {
        return;
    }
    ip_tcp_seg seg;
    if (!ip_tcp_parse(pkt, len, &seg)) {
        c->n_parse_drops++;
        return;
    }
    if (seg.dst_ip != c->lip || seg.dport != c->lport) {
        return;
    }

    if (c->listening && !c->peer_syn_seen) {
        if (!seg.syn || seg.rst) {
            return;
        }
        c->rip = seg.src_ip;
        c->rport = seg.sport;
    } else {
        if (seg.src_ip != c->rip || seg.sport != c->rport) {
            return;
        }
    }

    if (seg.rst) {
        c->rst = true;
        c->active = false;
        return;
    }

    bool rx_ok = receiver_on_segment(&c->rx, &c->out, seg.seq, seg.syn, seg.fin,
                                     seg.payload, seg.payload_len);

    if (seg.syn && !c->peer_syn_seen) {
        c->peer_syn_seen = true;
        if (seg.mss > 0 && seg.mss < c->tx.mss) {
            c->tx.mss = seg.mss;
        }
        if (c->listening) {
            c->active = true;
            sender_start(&c->tx);
        }
    }

    if (seg.ackf) {
        sender_on_ack(&c->tx, seg.ack, seg.window, now_ms);
    }

    if (seg.payload_len > 0 || seg.syn || seg.fin || !rx_ok) {
        c->need_ack = true;
    }
}

size_t tcp_conn_poll(tcp_conn *c, uint64_t now_ms, uint8_t *buf, size_t cap) {
    if (!c || !buf || c->rst) {
        return 0;
    }

    uint32_t ackno = 0;
    bool have_ack = receiver_ackno(&c->rx, &ackno);
    uint16_t win = receiver_window(&c->rx, &c->out);

    sender_out o;
    if (sender_poll(&c->tx, &c->in, now_ms, &o)) {
        ip_tcp_seg s = {
            .src_ip = c->lip,
            .dst_ip = c->rip,
            .sport = c->lport,
            .dport = c->rport,
            .seq = o.seq,
            .ack = have_ack ? ackno : 0u,
            .syn = o.syn,
            .ackf = have_ack,
            .fin = o.fin,
            .rst = false,
            .psh = (o.len > 0),
            .window = win,
            .mss = o.syn ? (uint16_t)c->mss : 0u,
            .ttl = 64,
            .payload = o.payload,
            .payload_len = o.len,
        };
        c->need_ack = false;
        size_t n = ip_tcp_build(buf, cap, &s, c->ip_id++);
        if (n > 0) {
            c->n_segs_sent++;
        }
        return n;
    }

    if (c->need_ack && have_ack) {
        c->need_ack = false;
        ip_tcp_seg s = {
            .src_ip = c->lip,
            .dst_ip = c->rip,
            .sport = c->lport,
            .dport = c->rport,
            .seq = tcp_wrap(c->tx.next_abs, c->tx.isn),
            .ack = ackno,
            .ackf = true,
            .window = win,
            .ttl = 64,
        };
        size_t n = ip_tcp_build(buf, cap, &s, c->ip_id++);
        if (n > 0) {
            c->n_segs_sent++;
        }
        return n;
    }

    return 0;
}

bool tcp_conn_done(const tcp_conn *c) {
    if (!c) {
        return false;
    }
    return c->rst || (sender_done(&c->tx) && bs_finished(&c->out));
}

size_t tcp_conn_write_input(tcp_conn *c, const char *data, size_t n) {
    if (!c) {
        return 0;
    }
    return bs_write(&c->in, data, n);
}

void tcp_conn_close_input(tcp_conn *c) {
    if (c) {
        bs_close(&c->in);
    }
}

size_t tcp_conn_read_output(tcp_conn *c, char *out, size_t n) {
    if (!c || !out || n == 0) {
        return 0;
    }
    size_t got = bs_peek(&c->out, out, n);
    bs_pop(&c->out, got);
    return got;
}

bool tcp_conn_output_finished(const tcp_conn *c) {
    return c ? bs_finished(&c->out) : false;
}

uint64_t tcp_conn_next_deadline(const tcp_conn *c) {
    return c ? sender_next_deadline(&c->tx) : UINT64_MAX;
}
