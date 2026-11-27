/* tcp_connection.h - CP3/CP4 TCP connection state machine (pure logic, no sockets/clock).
 * Purpose: wire ip_tcp, sender, receiver, and two 64 KiB bytestreams into one connection.
 * Usage:   tcp_conn_init -> tcp_conn_connect/listen -> on_packet/poll loop -> tcp_conn_free.
 * Status:  written, build not run.
 */
#ifndef SETU_TCP_CONNECTION_H
#define SETU_TCP_CONNECTION_H

#include "byte_stream.h"
#include "ip_tcp.h"
#include "receiver.h"
#include "sender.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t lip;
    uint32_t rip;
    uint16_t lport;
    uint16_t rport;
    uint32_t mss;
    uint16_t ip_id;

    bytestream in;
    bytestream out;
    sender tx;
    receiver rx;

    bool listening;
    bool peer_syn_seen;
    bool rst;
    bool active;
    bool need_ack;

    uint64_t n_segs_sent;
    uint64_t n_parse_drops;
} tcp_conn;

bool tcp_conn_init(tcp_conn *c, uint32_t lip, uint16_t lport,
                   uint32_t rip, uint16_t rport, uint32_t isn, uint32_t mss);
void tcp_conn_free(tcp_conn *c);
void tcp_conn_connect(tcp_conn *c);
void tcp_conn_listen(tcp_conn *c);
void tcp_conn_on_packet(tcp_conn *c, const uint8_t *pkt, size_t len, uint64_t now_ms);
size_t tcp_conn_poll(tcp_conn *c, uint64_t now_ms, uint8_t *buf, size_t cap);
bool tcp_conn_done(const tcp_conn *c);

size_t tcp_conn_write_input(tcp_conn *c, const char *data, size_t n);
void tcp_conn_close_input(tcp_conn *c);
size_t tcp_conn_read_output(tcp_conn *c, char *out, size_t n);
bool tcp_conn_output_finished(const tcp_conn *c);
uint64_t tcp_conn_next_deadline(const tcp_conn *c);

#endif /* SETU_TCP_CONNECTION_H */
