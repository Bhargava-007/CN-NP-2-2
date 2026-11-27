#!/usr/bin/env python3
"""
Purpose: Asyncio load client for CP2 concurrency cost curves (idle + active connections).
Usage:   python3 stack/measure/load_client.py --port P [--host 127.0.0.1] --idle N [--active 50] [--seconds 10] [--warmup 2]
Status:  written, not run.
"""

import argparse
import asyncio
import math
import time

REQ = b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"


def percentile(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return 0.0
    rank = math.ceil((p / 100.0) * len(sorted_vals))
    idx = max(0, min(len(sorted_vals) - 1, rank - 1))
    return sorted_vals[idx]


async def read_http_response(reader: asyncio.StreamReader, buf: bytearray) -> bool:
    while True:
        sep = buf.find(b"\r\n\r\n")
        if sep != -1:
            hdr_end = sep + 4
            cl = 3
            for line in bytes(buf[:sep]).split(b"\r\n")[1:]:
                if line.lower().startswith(b"content-length:"):
                    cl = int(line.split(b":", 1)[1].strip())
                    break
            total = hdr_end + cl
            while len(buf) < total:
                chunk = await reader.read(4096)
                if not chunk:
                    return False
                buf.extend(chunk)
            del buf[:total]
            return True
        chunk = await reader.read(4096)
        if not chunk:
            return False
        buf.extend(chunk)


async def open_idle(host: str, port: int, n: int) -> tuple[list[asyncio.StreamWriter], int]:
    writers: list[asyncio.StreamWriter] = []
    errors = 0
    batch = 500
    for start in range(0, n, batch):
        count = min(batch, n - start)
        coros = [asyncio.open_connection(host, port) for _ in range(count)]
        results = await asyncio.gather(*coros, return_exceptions=True)
        for res in results:
            if isinstance(res, Exception):
                errors += 1
            else:
                _r, w = res
                writers.append(w)
        if start + count < n:
            await asyncio.sleep(0.05)
    return writers, errors


async def active_worker(
    host: str,
    port: int,
    t_record_start: float,
    t_end: float,
    samples: list[float],
    err_counter: list[int],
) -> None:
    try:
        reader, writer = await asyncio.open_connection(host, port)
    except Exception:
        err_counter[0] += 1
        return

    buf = bytearray()
    try:
        while time.perf_counter() < t_end:
            t0 = time.perf_counter()
            writer.write(REQ)
            await writer.drain()
            ok = await read_http_response(reader, buf)
            t1 = time.perf_counter()
            if not ok:
                err_counter[0] += 1
                break
            if t1 >= t_record_start:
                samples.append((t1 - t0) * 1000.0)
    except Exception:
        err_counter[0] += 1
    finally:
        writer.close()


async def run(args: argparse.Namespace) -> None:
    idle_writers, idle_errors = await open_idle(args.host, args.port, args.idle)
    print(f"idle_opened={len(idle_writers)}/{args.idle}")

    t_now = time.perf_counter()
    t_record_start = t_now + args.warmup
    t_end = t_now + args.seconds
    samples: list[float] = []
    active_errors = [0]

    tasks = [
        asyncio.create_task(
            active_worker(args.host, args.port, t_record_start, t_end, samples, active_errors)
        )
        for _ in range(args.active)
    ]
    await asyncio.gather(*tasks)

    for w in idle_writers:
        w.close()

    total_errors = idle_errors + active_errors[0]
    measure_s = max(0.001, args.seconds - args.warmup)
    samples.sort()
    count = len(samples)
    rps = count / measure_s
    p50 = percentile(samples, 50.0)
    p95 = percentile(samples, 95.0)
    p99 = percentile(samples, 99.0)
    mx = samples[-1] if samples else 0.0

    print(
        f"idle_opened={len(idle_writers)} active={args.active} requests={count} "
        f"rps={rps:.1f} p50={p50:.3f} p95={p95:.3f} p99={p99:.3f} max={mx:.3f}"
    )
    if total_errors > 0:
        print(f"ERRORS={total_errors}")


def main() -> None:
    ap = argparse.ArgumentParser(description="CP2 concurrency cost-curve load client")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--idle", type=int, required=True)
    ap.add_argument("--active", type=int, default=50)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--warmup", type=float, default=2.0)
    args = ap.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
