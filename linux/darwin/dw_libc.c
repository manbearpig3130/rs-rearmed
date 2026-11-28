// libSystem's C library functions that glibc has under another name, with other numbers, or not at all.
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dw.h"

// __bzero: what Apple's compiler makes of a memset to zero.
void dw_bzero(void *p, size_t n) { memset(p, 0, n); }
DW_FUNCTION("___bzero", dw_bzero);

// memset_pattern16: the 16 bytes repeated over the buffer, the last repeat cut short.
void dw_memset_pattern16(void *dst, const void *pattern, size_t n) {
  char *d = dst;
  for (; n >= 16; d += 16, n -= 16) memcpy(d, pattern, 16);
  memcpy(d, pattern, n);
}
DW_FUNCTION("_memset_pattern16", dw_memset_pattern16);

// __exp10, __exp10f: what Apple's compiler makes of pow(10, x).
double dw_exp10(double x) { return exp10(x); }
DW_FUNCTION("___exp10", dw_exp10);
float dw_exp10f(float x) { return exp10f(x); }
DW_FUNCTION("___exp10f", dw_exp10f);

// __sincosf_stret: both at once, returned as a pair of floats in one register, as a C struct of two is.
struct dw_float2 {
  float sin, cos;
};
struct dw_float2 dw_sincosf_stret(float x) {
  struct dw_float2 r;
  sincosf(x, &r.sin, &r.cos);
  return r;
}
DW_FUNCTION("___sincosf_stret", dw_sincosf_stret);

// ___mb_cur_max: MB_CUR_MAX is a call on Darwin. One byte a character: the C locale.
int dw_mb_cur_max(void) { return 1; }
DW_FUNCTION("____mb_cur_max", dw_mb_cur_max);

// setlocale: the game stays in the C locale whatever it asks for (the rune table of dw_rune.c is that locale's,
// and numbers are read and written with a point on every machine), so nothing reaches the host's setlocale.
// The category numbers differ too (LC_ALL is 0 on Darwin and 6 here), which then does not matter.
char *dw_setlocale(int category, const char *locale) {
  (void)category, (void)locale;
  return "C";
}
DW_FUNCTION("_setlocale", dw_setlocale);

// strerror: of a Darwin number.
char *dw_strerror(int darwin) {
  static __thread char unknown[40];
  if (darwin == 0) return strerror(0);
  for (int host = 1; host < 140; host++)
    if (dw_errno_from_host(host) == darwin && !(darwin == 5 && host != EIO)) return strerror(host);
  snprintf(unknown, sizeof unknown, "Unknown error: %d", darwin);
  return unknown;
}
DW_FUNCTION("_strerror", dw_strerror);

// system: the game never runs a command. On the Mac the network guard stops the one the game has (opening a web
// page); here none starts at all.
int dw_system(const char *command) {
  dw_log("system(\"%s\") not run", command ? command : "");
  return command ? -1 : 0;  // system(NULL) asks whether there is a shell: no
}
DW_FUNCTION("_system", dw_system);

// --- signals ---
// The numbers differ from 7 on (Darwin's SIGBUS is 10, Linux's 7). Faults are the loader's to report, so the
// game's handlers for them are taken and never installed; to ignore a signal or restore its default is done;
// any other handler of the game's is noted and not installed, as nothing sends the game those signals.
static int host_signal(int darwin) {
  static const signed char k[] = {0, SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, -1 /* SIGEMT */, SIGFPE,
                                  SIGKILL, SIGBUS, SIGSEGV, SIGSYS, SIGPIPE, SIGALRM, SIGTERM, SIGURG, SIGSTOP,
                                  SIGTSTP, SIGCONT, SIGCHLD, SIGTTIN, SIGTTOU, SIGIO, SIGXCPU, SIGXFSZ, SIGVTALRM,
                                  SIGPROF, SIGWINCH, -1 /* SIGINFO */, SIGUSR1, SIGUSR2};
  return darwin > 0 && darwin < (int)sizeof k ? k[darwin] : -1;
}

static int is_fault(int host) {
  return host == SIGSEGV || host == SIGBUS || host == SIGILL || host == SIGFPE || host == SIGABRT || host == SIGTRAP;
}

static int set_disposition(int darwin, uintptr_t handler) {
  int host = host_signal(darwin);
  if (host < 0) {
    if (darwin == 7 || darwin == 29) return 0;  // a signal Linux has not: nothing ever sends it
    errno = EINVAL;
    return -1;
  }
  if (is_fault(host)) return 0;
  if (handler > 1) {  // 0 is SIG_DFL and 1 SIG_IGN, on both
    dw_log("a handler for signal %d (Darwin's number) is not installed", darwin);
    return 0;
  }
  return signal(host, handler ? SIG_IGN : SIG_DFL) == SIG_ERR ? -1 : 0;
}

void *dw_signal(int sig, void *handler) {
  if (set_disposition(sig, (uintptr_t)handler)) return (void *)-1;  // SIG_ERR
  return NULL;                                                      // the old one: the default
}
DW_FUNCTION("_signal", dw_signal);

// Darwin's struct sigaction: the handler, a 4-byte mask, the flags.
struct dw_sigaction {
  void *handler;
  uint32_t mask;
  int flags;
};

int dw_sigaction(int sig, const struct dw_sigaction *act, struct dw_sigaction *old) {
  if (old) memset(old, 0, sizeof *old);
  return act ? set_disposition(sig, (uintptr_t)act->handler) : 0;
}
DW_FUNCTION("_sigaction", dw_sigaction);

// --- the terminal: there is none ---
static int no_terminal(void) {
  errno = ENOTTY;
  return -1;
}
int dw_tcgetattr(int fd, void *termios) {
  (void)fd, (void)termios;
  return no_terminal();
}
DW_FUNCTION("_tcgetattr", dw_tcgetattr);

int dw_tcsetattr(int fd, int actions, const void *termios) {
  (void)fd, (void)actions, (void)termios;
  return no_terminal();
}
DW_FUNCTION("_tcsetattr", dw_tcsetattr);
