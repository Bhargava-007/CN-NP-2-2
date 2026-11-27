import socket, time, sys
port = int(sys.argv[1])
slow = socket.socket(); slow.connect(("127.0.0.1", port))
slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
req = b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"
slow.setblocking(False)
sent = 0
data = req * 1000
t0 = time.time()
while time.time() - t0 < 3:
    try:
        sent += slow.send(data)
    except BlockingIOError:
        time.sleep(0.01)
time.sleep(1)
c = socket.socket(); c.settimeout(2)
try:
    c.connect(("127.0.0.1", port)); c.sendall(req)
    print("second client:", "OK" if b"200" in c.recv(100) else "NO 200")
except Exception as e:
    print("second client: FAIL", e)
print("slow client bytes sent:", sent)
