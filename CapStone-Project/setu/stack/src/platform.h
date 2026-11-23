/* platform.h - one socket API for Windows (Winsock2, the primary target) and Linux (WSL). */
#ifndef SETU_PLATFORM_H
#define SETU_PLATFORM_H
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define SETU_BAD_SOCK INVALID_SOCKET
  #define sock_close closesocket
  #define sock_errno() WSAGetLastError()
  #define SETU_SHUT_WR SD_SEND
  typedef int sock_len_t;
  static inline int net_init(void) { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w); }
  static inline void net_cleanup(void) { WSACleanup(); }
  static inline int sock_set_nonblocking(sock_t s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on); }
  #define SETU_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK)
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  typedef int sock_t;
  #define SETU_BAD_SOCK (-1)
  #define sock_close close
  #define sock_errno() errno
  #define SETU_SHUT_WR SHUT_WR
  typedef socklen_t sock_len_t;
  static inline int net_init(void) { return 0; }
  static inline void net_cleanup(void) {}
  static inline int sock_set_nonblocking(sock_t s) { int f = fcntl(s, F_GETFL, 0); return fcntl(s, F_SETFL, f | O_NONBLOCK); }
  #define SETU_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#endif
#endif
