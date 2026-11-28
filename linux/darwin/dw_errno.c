// errno. The image was compiled with Darwin's numbers and reads errno through __error(). The first 34 agree with
// Linux's but for EDEADLK and EAGAIN, which trade places (11 and 35); above that nearly all differ.
//
// The game has a value of its own for each thread, numbered as Darwin numbers. At each look, a host errno that is
// set is moved into it, translated, and cleared: so a failure of any host function shows, and what the game writes
// (errno = 0 before a call it means to check) stays until the host fails again.
#include <errno.h>

#include "dw.h"

static const unsigned char k_darwin[] = {
    [EPERM] = 1, [ENOENT] = 2, [ESRCH] = 3, [EINTR] = 4, [EIO] = 5, [ENXIO] = 6, [E2BIG] = 7, [ENOEXEC] = 8,
    [EBADF] = 9, [ECHILD] = 10, [EDEADLK] = 11, [ENOMEM] = 12, [EACCES] = 13, [EFAULT] = 14, [ENOTBLK] = 15,
    [EBUSY] = 16, [EEXIST] = 17, [EXDEV] = 18, [ENODEV] = 19, [ENOTDIR] = 20, [EISDIR] = 21, [EINVAL] = 22,
    [ENFILE] = 23, [EMFILE] = 24, [ENOTTY] = 25, [ETXTBSY] = 26, [EFBIG] = 27, [ENOSPC] = 28, [ESPIPE] = 29,
    [EROFS] = 30, [EMLINK] = 31, [EPIPE] = 32, [EDOM] = 33, [ERANGE] = 34, [EAGAIN] = 35, [EINPROGRESS] = 36,
    [EALREADY] = 37, [ENOTSOCK] = 38, [EDESTADDRREQ] = 39, [EMSGSIZE] = 40, [EPROTOTYPE] = 41, [ENOPROTOOPT] = 42,
    [EPROTONOSUPPORT] = 43, [ESOCKTNOSUPPORT] = 44, [EOPNOTSUPP] = 45 /* Darwin's ENOTSUP */, [EPFNOSUPPORT] = 46,
    [EAFNOSUPPORT] = 47, [EADDRINUSE] = 48, [EADDRNOTAVAIL] = 49, [ENETDOWN] = 50, [ENETUNREACH] = 51,
    [ENETRESET] = 52, [ECONNABORTED] = 53, [ECONNRESET] = 54, [ENOBUFS] = 55, [EISCONN] = 56, [ENOTCONN] = 57,
    [ESHUTDOWN] = 58, [ETOOMANYREFS] = 59, [ETIMEDOUT] = 60, [ECONNREFUSED] = 61, [ELOOP] = 62, [ENAMETOOLONG] = 63,
    [EHOSTDOWN] = 64, [EHOSTUNREACH] = 65, [ENOTEMPTY] = 66, [EUSERS] = 68, [EDQUOT] = 69, [ESTALE] = 70,
    [EREMOTE] = 71, [ENOLCK] = 77, [ENOSYS] = 78, [EOVERFLOW] = 84, [ECANCELED] = 89, [EIDRM] = 90, [ENOMSG] = 91,
    [EILSEQ] = 92, [EBADMSG] = 94, [EMULTIHOP] = 95, [ENODATA] = 96, [ENOLINK] = 97, [ENOSR] = 98, [ENOSTR] = 99,
    [EPROTO] = 100, [ETIME] = 101, [ENOTRECOVERABLE] = 104, [EOWNERDEAD] = 105,
};

// A host number Darwin has no name for (ENOMEDIUM, EKEYEXPIRED, ...) becomes EIO: a failure with no closer name.
int dw_errno_from_host(int host) {
  if (host <= 0) return host;
  int e = (size_t)host < sizeof k_darwin ? k_darwin[host] : 0;
  return e ? e : 5;
}

static __thread int t_errno;

int *dw_error(void) {
  if (errno) {
    t_errno = dw_errno_from_host(errno);
    errno = 0;
  }
  return &t_errno;
}
DW_FUNCTION("___error", dw_error);
