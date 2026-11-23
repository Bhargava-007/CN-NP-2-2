/* setu_tcp.c - the Interop Gate harness (week 6). Builds and runs inside the lab engine,
   driven from PowerShell:  Invoke-SetuInteropGate -Profile jitter -Direction send
   It opens TUN device tun144, and moves raw IPv4 datagrams between the kernel and YOUR TCP.
   The connection logic (handshake, receiver, sender, timers) is CP3/CP4 - write it in
   tcp_connection.c using byte_stream.c and tcp_core.c, then replace the TODO below.            */
#ifdef __linux__
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "byte_stream.h"
#include "tcp_core.h"

static int tun_open(const char *name) {
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) { perror("open /dev/net/tun"); return -1; }
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;                 /* raw IP datagrams, no extra header */
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) { perror("TUNSETIFF"); close(fd); return -1; }
    return fd;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: setu_tcp connect <ip> <port> | listen <port>\n"); return 2; }
    int tun = tun_open("tun144");
    if (tun < 0) return 1;
    fprintf(stderr, "[setu_tcp] tun144 open, our address 169.254.144.9, mode=%s\n", argv[1]);
    /* TODO(CP3/CP4): create your connection object, send a SYN (connect) or wait for one (listen),
       then loop: read(tun) -> parse IPv4+TCP -> receiver/sender; stdin -> sender ByteStream;
       receiver ByteStream -> stdout; timers via tcp_core.c (rto_*) and reno_* for CP5.            */
    fprintf(stderr, "[setu_tcp] TCP connection not implemented yet - this is your CP3/CP4 work.\n");
    close(tun);
    return 3;
}
#else
#include <stdio.h>
int main(void) { puts("setu_tcp runs in the lab engine (Linux TUN). Use Invoke-SetuInteropGate from PowerShell."); return 0; }
#endif
