#include "../src/tcp_core.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* unwrap: around the 2^32 boundary, picks the closest absolute index */
    uint32_t isn = 0xFFFF0000u;
    for (uint64_t abs = 0; abs < (1ull << 33); abs += 0x1234567ull) {
        uint32_t w = tcp_wrap(abs, isn);
        uint64_t cp = abs > 1000 ? abs - 1000 : 0;
        assert(tcp_unwrap(w, cp, isn) == abs);
        assert(tcp_unwrap(w, abs + 1000, isn) == abs);
    }
    assert(tcp_unwrap(isn + 5, 0, isn) == 5);
    /* RTO: jitter around 50 ms keeps RTO well above the samples */
    rto_est e; rto_init(&e, 200, 60000);
    uint32_t s[] = {50, 60, 40, 55, 45, 70, 50};
    for (unsigned i = 0; i < sizeof s / sizeof s[0]; i++) rto_sample(&e, s[i]);
    assert(e.rto >= 200);
    rto_backoff(&e); uint32_t r1 = e.rto; rto_backoff(&e); assert(e.rto == 2 * r1 || e.rto == 60000);
    /* Reno: slow start doubles per RTT, 3 dup ACKs halve, timeout drops to 1 MSS */
    reno c; reno_init(&c, 1000);
    for (int i = 0; i < 30; i++) reno_on_new_ack(&c);
    assert(c.state == SLOW_START && c.cwnd == 32000);
    assert(!reno_on_dup_ack(&c) && !reno_on_dup_ack(&c) && reno_on_dup_ack(&c));
    assert(c.ssthresh == 16000 && c.cwnd == 19000 && c.state == FAST_RECOVERY);
    reno_on_new_ack(&c); assert(c.cwnd == 16000 && c.state == CONGESTION_AVOIDANCE);
    uint32_t before = c.cwnd; for (int i = 0; i < 16; i++) reno_on_new_ack(&c);
    assert(c.cwnd > before && c.cwnd <= before + 1000 + 50);   /* about +1 MSS per window of ACKs */
    reno_on_timeout(&c); assert(c.cwnd == 1000 && c.state == SLOW_START);
    assert(reno_send_window(&c, 500) == 500);
    printf("tcp_core: unwrap, RTO (RFC 6298 + Karn backoff), Reno - all PASS\n");
    return 0;
}
