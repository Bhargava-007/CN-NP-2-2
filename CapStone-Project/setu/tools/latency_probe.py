"""latency_probe.py - weeks 10 and 12: periodic small readings with send timestamps.
   Runs on lab devices (python3 is in the netshoot image). From PowerShell:
     Copy-SetuFile -Node collector -Path .\\tools\\latency_probe.py -Destination /root/p.py
     Copy-SetuFile -Node sensor    -Path .\\tools\\latency_probe.py -Destination /root/p.py
     Invoke-SetuNode collector 'python3 /root/p.py recv --port 5683 --count 3000 --out /root/run.csv'   (start first, in another window)
     Invoke-SetuNode sensor    'python3 /root/p.py send --to 10.20.48.30 --port 5683 --count 3000 --interval-ms 20'
   All lab devices share one kernel clock, so one-way delay is valid here."""
import argparse, math, socket, struct, time

def pct(v, p): return v[max(1, math.ceil(p / 100 * len(v))) - 1] if v else float('nan')

def send(a):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if a.tos: s.setsockopt(socket.IPPROTO_IP, socket.IP_TOS, a.tos)
    pad = b'\0' * max(0, a.size - 12)
    t_next = time.monotonic()
    for i in range(a.count):
        s.sendto(struct.pack('!Id', i, time.clock_gettime(time.CLOCK_REALTIME)) + pad, (a.to, a.port))
        t_next += a.interval_ms / 1000
        time.sleep(max(0, t_next - time.monotonic()))
    print(f'sent {a.count} readings of {a.size} B every {a.interval_ms} ms')

def recv(a):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('0.0.0.0', a.port)); s.settimeout(a.idle)
    lat, seen = [], set()
    try:
        while len(seen) < a.count:
            d, _ = s.recvfrom(2048)
            i, ts = struct.unpack('!Id', d[:12])
            seen.add(i); lat.append((i, (time.clock_gettime(time.CLOCK_REALTIME) - ts) * 1000))
    except socket.timeout:
        pass
    with open(a.out, 'w') as f:
        f.write('seq,latency_ms\n'); f.writelines(f'{i},{l:.4f}\n' for i, l in lat)
    v = sorted(l for _, l in lat)
    loss = 100 * (a.count - len(seen)) / a.count
    print(f'n={len(v)} loss={loss:.2f}% p50={pct(v,50):.3f} p99={pct(v,99):.3f} p99.9={pct(v,99.9):.3f} '
          f'jitter(p99-p50)={pct(v,99)-pct(v,50):.3f} ms -> {a.out}')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); sub = ap.add_subparsers(dest='cmd', required=True)
    s1 = sub.add_parser('send'); s1.add_argument('--to', required=True); s1.add_argument('--port', type=int, default=5683)
    s1.add_argument('--count', type=int, default=3000); s1.add_argument('--interval-ms', type=float, default=20)
    s1.add_argument('--size', type=int, default=40); s1.add_argument('--tos', type=lambda x: int(x, 0), default=0)
    s2 = sub.add_parser('recv'); s2.add_argument('--port', type=int, default=5683); s2.add_argument('--count', type=int, default=3000)
    s2.add_argument('--idle', type=float, default=5); s2.add_argument('--out', default='run.csv')
    a = ap.parse_args(); send(a) if a.cmd == 'send' else recv(a)
