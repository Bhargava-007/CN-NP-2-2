# Purpose: fake DNS server for testing dns_resolve edge cases (wrongid, wrongq, tc).
# Usage: python3 test_dns_fake.py wrongid|wrongq|tc
#        run ./dns_resolve host.test A 127.0.0.1 5400 in another shell.
# Note: wrong-source rejection is not covered by this script.
import socket, struct, sys, threading
mode = sys.argv[1]
def qsec_of(q):
    i = 12
    while q[i] != 0:
        i += 1 + q[i]
    return q[12:i+5]
def build(id_, qs, ip, flags=0x8180, ans=True):
    r = struct.pack(">HHHHHH", id_, flags, 1, 1 if ans else 0, 0, 0) + qs
    if ans:
        r += b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) + socket.inet_aton(ip)
    return r
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
u.bind(("127.0.0.1", 5400))
t = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
t.bind(("127.0.0.1", 5400))
t.listen(1)
def tcp():
    c, _ = t.accept()
    n = struct.unpack(">H", c.recv(2))[0]
    q = c.recv(n)
    r = build(struct.unpack(">H", q[:2])[0], qsec_of(q), "2.2.2.2")
    c.sendall(struct.pack(">H", len(r)) + r)
    c.close()
threading.Thread(target=tcp, daemon=True).start()
q, a = u.recvfrom(512)
id_ = struct.unpack(">H", q[:2])[0]
qs = qsec_of(q)
if mode == "wrongid":
    u.sendto(build(id_ ^ 0xFFFF, qs, "1.1.1.1"), a)
    u.sendto(build(id_, qs, "2.2.2.2"), a)
elif mode == "wrongq":
    u.sendto(build(id_, qs.replace(b"host", b"xxxx"), "1.1.1.1"), a)
    u.sendto(build(id_, qs, "2.2.2.2"), a)
elif mode == "tc":
    u.sendto(build(id_, qs, "", flags=0x8380, ans=False), a)
import time
time.sleep(1.5)
