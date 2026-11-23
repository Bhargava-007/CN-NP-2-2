/* tcp_core.h - CP3/CP4/CP5 building blocks, portable C, unit-tested on Windows (MSVC) and Linux.
   Your connection state machine (the graded part of CP3/CP4) calls these.                     */
#ifndef SETU_TCP_CORE_H
#define SETU_TCP_CORE_H
#include <stdbool.h>
#include <stdint.h>

/* CP3: 32-bit wire seqno -> 64-bit absolute index nearest the checkpoint. */
uint64_t tcp_unwrap(uint32_t seq, uint64_t checkpoint, uint32_t isn);
uint32_t tcp_wrap(uint64_t absolute, uint32_t isn);

/* CP4: RFC 6298 retransmission timer with Karn's algorithm. */
typedef struct { bool have_sample; uint32_t srtt, rttvar, rto; uint32_t min_ms, max_ms; } rto_est;
void rto_init(rto_est *e, uint32_t min_ms, uint32_t max_ms);
void rto_sample(rto_est *e, uint32_t rtt_ms);      /* call ONLY for segments never retransmitted */
void rto_backoff(rto_est *e);                      /* call on each timeout: doubles RTO         */

/* CP5: Reno congestion control, cwnd in BYTES. */
typedef enum { SLOW_START, CONGESTION_AVOIDANCE, FAST_RECOVERY } cc_state;
typedef struct { cc_state state; uint32_t cwnd, ssthresh, dup_acks, mss; } reno;
void     reno_init(reno *c, uint32_t mss);
void     reno_on_new_ack(reno *c);
bool     reno_on_dup_ack(reno *c);                 /* true = retransmit the oldest unacked now */
void     reno_on_timeout(reno *c);
uint32_t reno_send_window(const reno *c, uint32_t peer_rwnd);
#endif
