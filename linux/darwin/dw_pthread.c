// pthreads and the scheduler. glibc's objects are no larger than Darwin's (mutex 40 bytes in 64, condition 48 in
// 48, attributes 56 in 64), so the image's own storage holds them and most calls are the host's (`same`). Here
// are the ones where a number, a size or a first use differs. Every returned error number is Darwin's.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "dw.h"

// --- objects with Darwin's static initializer ---
// PTHREAD_MUTEX_INITIALIZER on Darwin is a signature in the first 8 bytes, which glibc would read as a mutex held
// by someone. glibc's own objects never begin with these values, so the signature tells an object that was never
// initialized by a call, and it is made then, once, under one lock of ours.
#define SIG_MUTEX 0x32AAABA7ull
#define SIG_MUTEX_ERRORCHECK 0x32AAABA1ull
#define SIG_MUTEX_RECURSIVE 0x32AAABA2ull
#define SIG_COND 0x3CB0B1BBull

static pthread_mutex_t g_first_use = PTHREAD_MUTEX_INITIALIZER;

static void make_mutex(pthread_mutex_t *m) {
  uint64_t sig = *(const uint64_t *)m;
  if (sig != SIG_MUTEX && sig != SIG_MUTEX_ERRORCHECK && sig != SIG_MUTEX_RECURSIVE) return;
  pthread_mutex_lock(&g_first_use);
  sig = *(const uint64_t *)m;
  if (sig == SIG_MUTEX || sig == SIG_MUTEX_ERRORCHECK || sig == SIG_MUTEX_RECURSIVE) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, sig == SIG_MUTEX_RECURSIVE ? PTHREAD_MUTEX_RECURSIVE
                                     : sig == SIG_MUTEX_ERRORCHECK ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
  }
  pthread_mutex_unlock(&g_first_use);
}

static void make_cond(pthread_cond_t *c) {
  if (*(const uint64_t *)c != SIG_COND) return;
  pthread_mutex_lock(&g_first_use);
  if (*(const uint64_t *)c == SIG_COND) pthread_cond_init(c, NULL);
  pthread_mutex_unlock(&g_first_use);
}

int dw_pthread_mutex_lock(pthread_mutex_t *m) {
  make_mutex(m);
  return DW_ERR(pthread_mutex_lock(m));
}
DW_FUNCTION("_pthread_mutex_lock", dw_pthread_mutex_lock);

int dw_pthread_mutex_trylock(pthread_mutex_t *m) {
  make_mutex(m);
  return DW_ERR(pthread_mutex_trylock(m));
}
DW_FUNCTION("_pthread_mutex_trylock", dw_pthread_mutex_trylock);

int dw_pthread_mutex_unlock(pthread_mutex_t *m) {
  make_mutex(m);
  return DW_ERR(pthread_mutex_unlock(m));
}
DW_FUNCTION("_pthread_mutex_unlock", dw_pthread_mutex_unlock);

int dw_pthread_mutex_destroy(pthread_mutex_t *m) {
  make_mutex(m);
  return DW_ERR(pthread_mutex_destroy(m));
}
DW_FUNCTION("_pthread_mutex_destroy", dw_pthread_mutex_destroy);

int dw_pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
  make_cond(c);
  make_mutex(m);
  return DW_ERR(pthread_cond_wait(c, m));
}
DW_FUNCTION("_pthread_cond_wait", dw_pthread_cond_wait);

// The time is absolute and the struct is the same; the timeout's number is not (60 on Darwin, 110 here).
int dw_pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *until) {
  make_cond(c);
  make_mutex(m);
  return DW_ERR(pthread_cond_timedwait(c, m, until));
}
DW_FUNCTION("_pthread_cond_timedwait", dw_pthread_cond_timedwait);

int dw_pthread_cond_signal(pthread_cond_t *c) {
  make_cond(c);
  return DW_ERR(pthread_cond_signal(c));
}
DW_FUNCTION("_pthread_cond_signal", dw_pthread_cond_signal);

int dw_pthread_cond_broadcast(pthread_cond_t *c) {
  make_cond(c);
  return DW_ERR(pthread_cond_broadcast(c));
}
DW_FUNCTION("_pthread_cond_broadcast", dw_pthread_cond_broadcast);

// --- numbers ---

// A key is 8 bytes on Darwin and 4 here: all 8 are written. By value it passes either way.
int dw_pthread_key_create(unsigned long *key, void (*destructor)(void *)) {
  pthread_key_t k;
  int r = pthread_key_create(&k, destructor);
  if (!r) *key = k;
  return DW_ERR(r);
}
DW_FUNCTION("_pthread_key_create", dw_pthread_key_create);

// Darwin: 0 normal, 1 error-checking, 2 recursive. glibc has the last two the other way round.
int dw_pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
  static const int k_type[] = {PTHREAD_MUTEX_NORMAL, PTHREAD_MUTEX_ERRORCHECK, PTHREAD_MUTEX_RECURSIVE};
  if (type < 0 || type > 2) return 22;  // EINVAL
  return DW_ERR(pthread_mutexattr_settype(attr, k_type[type]));
}
DW_FUNCTION("_pthread_mutexattr_settype", dw_pthread_mutexattr_settype);

// Darwin: 1 joinable, 2 detached. glibc: 0 and 1.
int dw_pthread_attr_setdetachstate(pthread_attr_t *attr, int state) {
  if (state != 1 && state != 2) return 22;
  return DW_ERR(pthread_attr_setdetachstate(attr, state == 2 ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE));
}
DW_FUNCTION("_pthread_attr_setdetachstate", dw_pthread_attr_setdetachstate);

// --- scheduling ---
// The game asks for a policy and a priority between 15 and 47 for its threads (macOS's range, see
// dw_sched_get_priority_min). Linux gives an ordinary thread one policy with one priority, 0, and refuses the
// real-time ones without a privilege: a thread created with one would fail to start. So what the game asks for
// is accepted and kept for it to read back, and every thread runs with the default policy.

// Darwin: 1 inherit, 2 explicit. Explicit would make glibc apply the attributes' policy and priority at creation.
int dw_pthread_attr_setinheritsched(pthread_attr_t *attr, int inherit) {
  (void)attr;
  return inherit == 1 || inherit == 2 ? 0 : 22;
}
DW_FUNCTION("_pthread_attr_setinheritsched", dw_pthread_attr_setinheritsched);

int dw_pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inherit) {
  (void)attr;
  *inherit = 1;
  return 0;
}
DW_FUNCTION("_pthread_attr_getinheritsched", dw_pthread_attr_getinheritsched);

// Darwin: SCHED_OTHER 1, SCHED_RR 2, SCHED_FIFO 4.
int dw_pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy) {
  (void)attr;
  return policy == 1 || policy == 2 || policy == 4 ? 0 : 22;
}
DW_FUNCTION("_pthread_attr_setschedpolicy", dw_pthread_attr_setschedpolicy);

// Darwin: 1 system, 2 process. Linux has the first alone.
int dw_pthread_attr_setscope(pthread_attr_t *attr, int scope) {
  (void)attr;
  return scope == 1 || scope == 2 ? 0 : 22;
}
DW_FUNCTION("_pthread_attr_setscope", dw_pthread_attr_setscope);

// Each thread's priority as the game last set it, for the game to read back: 31 is macOS's default.
static __thread int t_priority = 31;
static __thread int t_policy = 1;

int dw_pthread_setschedparam(pthread_t thread, int policy, const int *param) {
  if (policy != 1 && policy != 2 && policy != 4) return 22;
  if (pthread_equal(thread, pthread_self())) t_policy = policy, t_priority = *param;
  return 0;
}
DW_FUNCTION("_pthread_setschedparam", dw_pthread_setschedparam);

int dw_pthread_getschedparam(pthread_t thread, int *policy, int *param) {
  int own = pthread_equal(thread, pthread_self());
  *policy = own ? t_policy : 1;
  *param = own ? t_priority : 31;
  return 0;
}
DW_FUNCTION("_pthread_getschedparam", dw_pthread_getschedparam);

// macOS answers 15 and 47 for every policy, and the game places its threads' priorities between the two.
int dw_sched_get_priority_min(int policy) {
  (void)policy;
  return 15;
}
DW_FUNCTION("_sched_get_priority_min", dw_sched_get_priority_min);

int dw_sched_get_priority_max(int policy) {
  (void)policy;
  return 47;
}
DW_FUNCTION("_sched_get_priority_max", dw_sched_get_priority_max);

// --- threads ---

// The error numbers. The game's code runs on the thread as it is, on a stack of the size the game asked for (the
// translated build needs larger ones; this one does not).
int dw_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
  return DW_ERR(pthread_create(thread, attr, start, arg));
}
DW_FUNCTION("_pthread_create", dw_pthread_create);

// On Darwin it names the calling thread and takes the name alone; Linux keeps 15 characters.
int dw_pthread_setname_np(const char *name) {
  char buf[16];
  strncpy(buf, name ? name : "", sizeof buf - 1);
  buf[sizeof buf - 1] = 0;
  return DW_ERR(pthread_setname_np(pthread_self(), buf));
}
DW_FUNCTION("_pthread_setname_np", dw_pthread_setname_np);

int dw_pthread_yield_np(void) {
  sched_yield();
  return 0;
}
DW_FUNCTION("_pthread_yield_np", dw_pthread_yield_np);

// pthread_mach_thread_np: the thread's Mach port, which the game uses as a number for the thread. The kernel's
// thread id is that here; for another thread than the caller glibc gives it from 2.42 on, and before that a
// number made of the thread's handle stands in.
unsigned dw_pthread_mach_thread_np(pthread_t thread) {
  if (pthread_equal(thread, pthread_self())) return (unsigned)syscall(SYS_gettid);
  static int (*gettid_np)(pthread_t);
  static int looked;
  if (!looked) gettid_np = (int (*)(pthread_t))dlsym(RTLD_DEFAULT, "pthread_gettid_np"), looked = 1;
  if (gettid_np) return (unsigned)gettid_np(thread);
  uint64_t h = (uint64_t)thread * 0x9e3779b97f4a7c15ull;
  return (unsigned)(h >> 33) | 0x40000000u;
}
DW_FUNCTION("_pthread_mach_thread_np", dw_pthread_mach_thread_np);
