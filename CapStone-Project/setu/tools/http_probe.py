"""http_probe.py - week 4 CP1 acceptance tests against your server, from Windows.
   python tools\\http_probe.py --port 8081"""
import argparse, socket, time

def talk(port, chunks, delay=0.0):
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    for c in chunks:
        s.sendall(c)
        if delay: time.sleep(delay)
    data = b''
    try:
        while True:
            d = s.recv(65536)
            if not d: break
            data += d
    except socket.timeout:
        pass
    s.close()
    return data

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--port', type=int, default=8081); a = ap.parse_args()
    req = b"GET /probe HTTP/1.1\r\nHost: setu.local\r\nConnection: close\r\n\r\n"
    r1 = talk(a.port, [bytes([b]) for b in req], delay=0.002)
    print('one byte per send      :', r1.split(b'\r\n')[0].decode(errors='replace'))
    r2 = talk(a.port, [req.replace(b'close', b'keep-alive') * 2 + req])
    print('three in one send      :', r2.count(b'HTTP/1.1 '), 'responses (CP1 target: 3; the starter server answers 1)')
    bad = b"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nabc"
    print('CL + TE together       :', talk(a.port, [bad]).split(b'\r\n')[0].decode(errors='replace'), '(must be 400)')
    print('missing Host           :', talk(a.port, [b"GET / HTTP/1.1\r\n\r\n"]).split(b'\r\n')[0].decode(errors='replace'), '(must be 400)')

if __name__ == '__main__':
    main()
