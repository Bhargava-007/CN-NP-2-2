# Edge Latency & Jitter Budget (`BUDGET.md` v0)

## 1. Course Target

- **Delay:** `p50 <= 8 ms`, `p99 <= 20 ms`
- **Jitter (`p99 - p50`):** `<= 12 ms`
- **Loss:** `<= 0.1%` per `60 s`
- **Background load:** `200 Mbit/s` bulk transfer sharing the path
- **Reporting rule:** Always report `p50` / `p95` / `p99`, never a mean.

## 2. Term-by-Term Budget (v0)

Measured baseline and queue-bounding facts (all `dev01 -> srv01`, host CPU not measured):
- Idle RTT: `p50 0.09 ms` / `p99 0.269 ms` (1000 probes, 3 runs, host idle)
- `netem` 1000-pkt queue at `100 Mbit/s` under TCP load: `p50 269 ms` / `p99 329 ms` (1 run)
- `netem` 50-pkt queue at `100 Mbit/s` under TCP load: `p50 ~55 ms` / `p99 ~65 ms` (6 runs)
- `tbf` `latency 10ms` on one egress at `99.2 Mbit/s`: `p50 3.86 ms` / `p99 5.0 ms` (3 runs)
- Queueing knee between `95M` and `100M` offered: `p50 0.06 -> 7.2 ms`
- Packet-count queue limits do not bound delay (queued packets were `14-19 KB`); byte/time limits do.

| Term | v0 value | Basis | Confidence | To replace in week 12 by |
| :--- | :--- | :--- | :--- | :--- |
| Sensor / app processing | not measured | Unmeasured in week 2 | LOW | End-to-end app timestamping under load |
| Queueing delay behind bulk | `3.86 ms` p50 / `5.0 ms` p99 (at `100 Mbit/s`, 1 TCP flow, 1 egress shaped with `tbf latency 10ms`); `269 ms` / `55 ms` unshaped (`netem` 1000 / 50 pkts) | MEASURED (`tbf` 3 runs; `netem` 1 / 6 runs) | MED | `200 Mbit/s` bulk + EF flow with byte/time-bounded queue + EF priority |
| Serialization per hop | `7.8 us/hop` at `100 Mbit/s`; `0.78 us/hop` at `1 Gbit/s` (98 B frame) | COMPUTED | HIGH | Measured wire rate at `200 Mbit/s` bottleneck |
| Propagation | `~0 ms` | COMPUTED (virtual `veth` links) | HIGH | Unchanged (virtual links) |
| Forwarding / processing per element | `~13 us` per element per direction | INFERRED from idle RTT `p50 0.078 ms` | MED | Measured forwarding delay under `200 Mbit/s` load |
| Clock sync error | not measured | Unmeasured in week 2 | LOW | One-way vs RTT clock-skew measurement |
| Shaper / policer delay | Included in `tbf` `3.86 ms` p50 / `5.0 ms` p99 (`burst 16kb`, `latency 10ms`) | MEASURED (`tbf` 3 runs) | MED | Separate shaper/policer characterization at `200 Mbit/s` |
| Multicast replication | not measured | Unmeasured in week 2 | LOW | Multicast fan-out measurement if used |
| Retransmission / loss recovery | not measured | Unmeasured in week 2 (`0.1%` target needs thousands of probes/run) | LOW | `60 s` loss run (`>= 10,000` probes) under `200 Mbit/s` load |

## 3. Verdict (v0): NOT YET PROVEN

`200 Mbit/s` with an EF flow is unmeasured; the `3.9 ms` (`p50 3.86 ms`) result was `100 Mbit/s`, 1 TCP flow, and only one direction shaped. If the budget fails under `200 Mbit/s` bulk load, the single term to change is **queueing delay behind bulk**, fixed by a byte/time-bounded queue plus priority for EF (week 12).

## 4. Open Items

- Measure `200 Mbit/s` bulk transfer + EF flow simultaneously (both directions).
- Run `60 s` loss test with enough probes (`>= 10,000`) to verify `<= 0.1%` loss.
- Measure sensor/app processing, clock sync error, and multicast replication (if applicable).
- Identify why queued packets reached `14-19 KB` even with `gso_max_size 1514` and offloads off.
- Fix `Measure-SetuThroughput -Runs 1` `StrictMode` `Count` bug so single-run throughput/drops are captured cleanly.
