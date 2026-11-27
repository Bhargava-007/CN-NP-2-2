# Accept Queue Behaviour (`ACCEPT-QUEUE.md`)

## Setup
- Server: `server_models epoll`, port `18084`, `backlog 8`, `somaxconn 4096`
- Clients: `nc -z`, `3 s` timeout
- Runs: 1 run

## Observations

- **A (`no-STOP`, 50 conns, MEASURED):** `ListenOverflows 0`, `ListenDrops 0`.
- **B (`kill -STOP`, 200 conns, MEASURED):** `ListenOverflows 530`, `ListenDrops 530`; `ss` showed `Recv-Q 9` vs `Send-Q 8` (`backlog + 1`).
- **C (`kill -CONT`, MEASURED):** `Recv-Q 0`, counters `564/564`, server answered `HTTP/1.1 200 OK`.
- **Counter vs connection count (INFERRED):** Counters exceed connection count (`530`/`564` vs `200`) because the kernel counts retried SYN/ACK events; not counted directly.

## Limits & Invalid First Attempt

- **Not tested:** Shipped `server_epoll` (`backlog 4096` cannot overflow at this flood size); only one run.
- **Invalid first attempt:** Wrong port (`server_epoll` takes `<et|lt> <port>`; port `8080` belonged to `docker-proxy`).
