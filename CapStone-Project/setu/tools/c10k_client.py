"""c10k_client.py - week 5: open N idle TCP connections to your server and hold them.
   PowerShell:  python tools\\c10k_client.py --port 8082 --count 10000
   Watch the server's memory meanwhile:  Get-Process server_models | Select WorkingSet64"""
import argparse, asyncio, time

async def one(host, port, hold, conns):
    try:
        r, w = await asyncio.open_connection(host, port)
        conns.append(w)
        await asyncio.sleep(hold)
        w.close()
    except OSError as e:
        conns.append(e)

async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1'); ap.add_argument('--port', type=int, default=8082)
    ap.add_argument('--count', type=int, default=10000); ap.add_argument('--hold', type=float, default=60)
    a = ap.parse_args()
    conns, t0 = [], time.time()
    tasks = []
    for i in range(a.count):
        tasks.append(asyncio.create_task(one(a.host, a.port, a.hold, conns)))
        if i % 500 == 499: await asyncio.sleep(0.05)          # ramp gently: do not overflow the accept queue
    await asyncio.sleep(2)
    ok = sum(1 for c in conns if not isinstance(c, Exception))
    print(f"open idle connections: {ok}/{a.count} after {time.time()-t0:.1f} s; holding {a.hold:.0f} s")
    errs = [c for c in conns if isinstance(c, Exception)]
    if errs: print(f"first error: {errs[0]!r}  (raise limits or slow the ramp)")
    await asyncio.gather(*tasks, return_exceptions=True)

if __name__ == '__main__':
    asyncio.run(main())
