"""lowpan_frag.py - week 10: what fragmentation does to loss on a 127-byte link.
   Splits each message into frames of at most --frame-payload bytes (74 for SETU-EDGE) and counts
   a message delivered only if EVERY frame arrives. Run on two lab devices with netem loss between them:
     Set-SetuImpairment -Node r-edge -Interface eth2 -LossPct 1
     Invoke-SetuNode collector 'python3 /root/f.py recv --port 6000'
     Invoke-SetuNode sensor    'python3 /root/f.py send --to 10.20.48.30 --port 6000 --message-bytes 1280'"""
import argparse, math, socket, struct, time

def send(a):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    n = math.ceil(a.message_bytes / a.frame_payload)
    for m in range(a.messages):
        for f in range(n):
            s.sendto(struct.pack('!IHH', m, f, n) + b'\0' * a.frame_payload, (a.to, a.port))
        time.sleep(a.gap_ms / 1000)
    s.sendto(b'END', (a.to, a.port))
    print(f'{a.messages} messages x {n} frames each ({a.message_bytes} B / {a.frame_payload} B)')

def recv(a):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('0.0.0.0', a.port)); s.settimeout(10)
    got, frames, n = {}, 0, 1
    try:
        while True:
            d, _ = s.recvfrom(4096)
            if d == b'END': break
            m, f, n = struct.unpack('!IHH', d[:8]); got.setdefault(m, set()).add(f); frames += 1
    except socket.timeout:
        pass
    msgs = max(got) + 1 if got else 0
    complete = sum(1 for m in got if len(got[m]) == n)
    p = 1 - frames / (msgs * n) if msgs else float('nan')
    print(f'frames/message={n}  frame loss={100*p:.2f}%  message loss={100*(1-complete/msgs):.2f}%  '
          f'predicted 1-(1-p)^n={100*(1-(1-p)**n):.2f}%  amplification={((1-complete/msgs)/p) if p>0 else float("nan"):.1f}x')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); sub = ap.add_subparsers(dest='cmd', required=True)
    s1 = sub.add_parser('send'); s1.add_argument('--to', required=True); s1.add_argument('--port', type=int, default=6000)
    s1.add_argument('--messages', type=int, default=2000); s1.add_argument('--message-bytes', type=int, default=1280)
    s1.add_argument('--frame-payload', type=int, default=74); s1.add_argument('--gap-ms', type=float, default=5)
    s2 = sub.add_parser('recv'); s2.add_argument('--port', type=int, default=6000)
    a = ap.parse_args(); send(a) if a.cmd == 'send' else recv(a)
