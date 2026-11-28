// The network: there is none. The game never calls home (the project's offline rule), and here that is by
// construction: its socket imports are these functions, which make no socket and reach nothing, as a machine with
// no network answers. Nothing in this file may call the host's socket functions.
#include <errno.h>
#include <stddef.h>

#include "dw.h"

static int no_socket(void) {
  errno = EBADF;  // no descriptor of the game's is a socket
  return -1;
}

int dw_socket(int domain, int type, int protocol) {
  (void)domain, (void)type, (void)protocol;
  errno = EAFNOSUPPORT;
  return -1;
}
DW_FUNCTION("_socket", dw_socket);

int dw_connect(int fd, const void *addr, unsigned len) { return (void)fd, (void)addr, (void)len, no_socket(); }
DW_FUNCTION("_connect", dw_connect);
int dw_bind(int fd, const void *addr, unsigned len) { return (void)fd, (void)addr, (void)len, no_socket(); }
DW_FUNCTION("_bind", dw_bind);
long dw_send(int fd, const void *buf, size_t n, int flags) { return (void)fd, (void)buf, (void)n, (void)flags, no_socket(); }
DW_FUNCTION("_send", dw_send);
long dw_sendto(int fd, const void *buf, size_t n, int flags, const void *addr, unsigned len) {
  return (void)fd, (void)buf, (void)n, (void)flags, (void)addr, (void)len, no_socket();
}
DW_FUNCTION("_sendto", dw_sendto);
long dw_recv(int fd, void *buf, size_t n, int flags) { return (void)fd, (void)buf, (void)n, (void)flags, no_socket(); }
DW_FUNCTION("_recv", dw_recv);
long dw_recvfrom(int fd, void *buf, size_t n, int flags, void *addr, unsigned *len) {
  return (void)fd, (void)buf, (void)n, (void)flags, (void)addr, (void)len, no_socket();
}
DW_FUNCTION("_recvfrom", dw_recvfrom);
int dw_getsockopt(int fd, int level, int name, void *value, unsigned *len) {
  return (void)fd, (void)level, (void)name, (void)value, (void)len, no_socket();
}
DW_FUNCTION("_getsockopt", dw_getsockopt);
int dw_setsockopt(int fd, int level, int name, const void *value, unsigned len) {
  return (void)fd, (void)level, (void)name, (void)value, (void)len, no_socket();
}
DW_FUNCTION("_setsockopt", dw_setsockopt);
int dw_getsockname(int fd, void *addr, unsigned *len) { return (void)fd, (void)addr, (void)len, no_socket(); }
DW_FUNCTION("_getsockname", dw_getsockname);
int dw_getpeername(int fd, void *addr, unsigned *len) { return (void)fd, (void)addr, (void)len, no_socket(); }
DW_FUNCTION("_getpeername", dw_getpeername);
int dw_shutdown(int fd, int how) { return (void)fd, (void)how, no_socket(); }
DW_FUNCTION("_shutdown", dw_shutdown);

// No name resolves.
void *dw_gethostbyname(const char *name) { return (void)name, NULL; }
DW_FUNCTION("_gethostbyname", dw_gethostbyname);

// No interfaces: an empty list, which is a success.
int dw_getifaddrs(void **list) {
  *list = NULL;
  return 0;
}
DW_FUNCTION("_getifaddrs", dw_getifaddrs);
void dw_freeifaddrs(void *list) { (void)list; }
DW_FUNCTION("_freeifaddrs", dw_freeifaddrs);
