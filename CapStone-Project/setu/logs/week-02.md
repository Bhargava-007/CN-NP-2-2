# Week 2 log (2026-11-24)

## Week 2 results so far (MEASURED, dev01 -> srv01, 1 flow, 56 B ICMP, offload off on dev01/srv01 only, host CPU not measured)
- **Date:** 2026-11-24
- **Baseline:** `iperf3` TCP 3 x 30 s (omit 5): `6228.9 / 6244.4 / 6148.7` Mbit/s, median `6228.9` Mbit/s. RTT p50 `0.088` ms, p99 `0.187` ms (200 probes), `0%` loss.
- **Delay 10 ms +/- 1 ms each way (3 x 200 probes):** p50 `20.1 / 20.1 / 20.4` ms, p95 `22.8 / 22.6 / 22.5` ms, p99 `23.3 / 23.8 / 23.4` ms, max `23.8 / 24.0 / 23.8` ms; median p50 `20.1` ms, p99 `23.4` ms; `0%` loss. Cleared; baseline p50 `0.088` ms confirmed.
- **Loss 1% each way (3 x 200 probes):** received `193 / 194 / 197` (`3.5% / 3.0% / 1.5%` loss), pooled `16/600 = 2.7%` vs `1.99%` expected; p50 about `0.09` ms unchanged. Cleared; baseline confirmed. Note: `0.1%` loss target needs thousands of probes per run.
- **Rate 100 Mbit/s, limit 1000, idle (1 run x 200 probes; currently NOT active, lab redeployed):** p50 `0.128` ms, p95 `0.179` ms, p99 `0.299` ms, max `0.352` ms, `0%` loss (needs 3 runs). Serialization `7.8 us/hop x 6 = about 47 us` (COMPUTED).
- **Lab note:** `netem` is egress-only, so impairments were set on both ends; lab restarted after shutdown and was redeployed with keepalive.
- **Verification:** Log entry only; nothing tested here.
- **Files changed:** `CapStone-Project\setu\logs\week-02.md`
- **Suggested commit message:** `docs(w02): log week 2 baseline and netem measurements`

## Bufferbloat results (MEASURED, dev01 -> srv01, 1 TCP flow iperf3, ICMP probes during load, 100 Mbit/s on dev01 egress, host CPU not measured)
- **Date:** 2026-11-24
- **Idle at 100 Mbit/s, `netem` limit 1000 pkts (1 run x 200):** p50 `0.128` ms, p99 `0.299` ms.
- **Loaded, `netem` limit 1000 pkts (1 run x 100):** p50 `269` ms, p95 `322` ms, p99 `329` ms; throughput not captured (`Measure-SetuThroughput -Runs 1` fails under `StrictMode`: `Count` property).
- **Loaded, `netem` limit 50 pkts (6 runs):** p50 `57.5 / 61.4 / 54.4 / 59.0 / 53.7 / 60.2` ms, p99 `59.9-71.1` ms; `99.3` Mbit/s. Backlog mid-load `44-47` packets, `628-885` KB (about `14-19` KB per packet), so `50 ms+` of queued data (COMPUTED). Prediction of `~6 ms` was wrong: it assumed `1500 B` packets.
- **Tried, NO EFFECT:** `tso/gso/gro` off on `dev01` `eth1` (p50 `53.7-60.2` ms); `gso_max_size 1514` (p50 `49.1-56.7` ms, backlog `770` KB / `45` pkts). Interfaces have MTU `9500`. Cause of large queued packets not identified.
- **Fix (`tbf rate 100mbit burst 16kb latency 10ms` on `dev01` egress):** loaded p50 `3.86 / 3.91 / 3.81` ms, p99 `4.92 / 5.01 / 4.74` ms, `99.2` Mbit/s, backlog `57` KB / `6` pkts (`4.6` ms, COMPUTED). Only `dev01` egress changed; `srv01` ACK direction unchanged.
- **Conclusion:** Bound queues in bytes/time, not packets. Cleared; baseline p50 `0.089` ms, p99 `0.181` ms confirmed; `gso_max_size` restored.
- **Verification:** Log entry only; nothing tested here.
- **Files changed:** `CapStone-Project\setu\logs\week-02.md`
- **Suggested commit message:** `docs(w02): log bufferbloat and tbf queue-bounding measurements`

## Queueing knee (MEASURED, dev01 -> srv01, UDP iperf3 offered load, tbf rate 100mbit burst 16kb latency 50ms on dev01 egress, ICMP probes during load, host CPU not measured)
- **Date:** 2026-11-24
- **First pass (1 run x 60 probes):** `50M` p50 `0.063` ms, p99 `0.203` ms; `80M` `0.061 / 2.4` ms (outlier); `90M` `0.061 / 0.274` ms; `95M` `0.059 / 0.170` ms; `105M` `7.29 / 10.5` ms.
- **Second pass (3 x 100 probes):** `95M` p50 `0.062 / 0.061 / 0.061` ms, p99 `3.15 / 0.254 / 0.497` ms; `100M` p50 `7.27 / 7.16 / 7.11` ms, p99 `9.77 / 9.91 / 9.87` ms; `105M` p50 `7.23 / 7.10 / 7.11` ms, p99 `10.0 / 9.78 / 9.81` ms. Ping loss `0%` everywhere.
- **Observations:** Knee between `95M` and `100M` offered; p50 jumps about `117x`. Above capacity, delay plateaus near `7 ms`, not the `50 ms` queue ceiling; cause not investigated. UDP throughput and queue drops not captured (`Measure-SetuThroughput -Runs 1` `StrictMode` bug).
- **Cleanup:** Cleared; baseline p50 `0.091` ms, p99 `0.285` ms confirmed.
- **Verification:** Log entry only; nothing tested here.
- **Files changed:** `CapStone-Project\setu\logs\week-02.md`
- **Suggested commit message:** `docs(w02): log queueing knee measurements`
