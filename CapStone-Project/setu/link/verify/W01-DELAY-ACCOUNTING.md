# Week 1 Delay Accounting (`W01-DELAY-ACCOUNTING`)

## 1. Measured RTT (MEASURED)

3 runs of 1000 ICMP probes `dev01` -> `10.20.50.10` (`0%` loss each), in ms:

| Run | p50 (ms) | p95 (ms) | p99 (ms) | max (ms) |
| :--- | :--- | :--- | :--- | :--- |
| run1 | 0.089 | 0.153 | 0.204 | 0.423 |
| run2 | 0.078 | 0.138 | 0.201 | 0.401 |
| run3 | 0.065 | 0.127 | 0.167 | 0.391 |
| **Median** | **0.078** | **0.138** | **0.201** | **0.401** |

- **Range:** p50 `0.065-0.089` ms, p99 `0.167-0.204` ms.

## 2. Conditions

- 1 flow, 56 B ICMP payload (98 B frame)
- Offload off on `dev01` and `srv01` only (`rtr` has no `ethtool`)
- Default `ping` interval
- `veth` links in containers on WSL2
- Host CPU load not measured
- Run 1 may include warm-up

## 3. Delay Terms

Frame size `98 B`, 6 serializations per round trip (3 forwarding elements each way):

| Term | Label | Value / Notes |
| :--- | :--- | :--- |
| `d_trans` | COMPUTED | ~0 on `veth` (`0.78 us/hop` at a nominal `1 Gbit/s` = `~4.7 us` RTT; `78 us/hop` at `10 Mbit/s`) |
| `d_prop` | COMPUTED | ~0 |
| `d_queue` | ESTIMATE | ~0 (idle, one flow) |
| `d_proc` | INFERRED | By subtraction, nearly all of the `~78 us` median; roughly `13 us` per forwarding element per direction (rough) |

## 4. Conclusions

- Delay here is processing (`d_proc`), not transmission (`d_trans`) or propagation (`d_prop`).
- The `p99 - p50` spread (`~0.12 ms`) is scheduler jitter, not queueing.
- This is the opposite of a real campus link, so it is a limit of the lab.
- `d_queue` is the term week 2 will move.
