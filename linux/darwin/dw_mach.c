// Mach, and libkern's atomics: the clock, semaphores, ports.
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include "dw.h"

enum { KERN_SUCCESS = 0, KERN_INVALID_ARGUMENT = 4, KERN_RESOURCE_SHORTAGE = 6, KERN_INVALID_NAME = 15 };

// mach_absolute_time in nanoseconds, so that the timebase is 1/1 (as it is on an Intel Mac).
uint64_t dw_mach_absolute_time(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
DW_FUNCTION("_mach_absolute_time", dw_mach_absolute_time);

struct dw_timebase {
  uint32_t numer, denom;
};
int dw_mach_timebase_info(struct dw_timebase *info) {
  info->numer = info->denom = 1;
  return KERN_SUCCESS;
}
DW_FUNCTION("_mach_timebase_info", dw_mach_timebase_info);

// --- semaphores ---
// A Mach semaphore is a port number; behind each is a POSIX semaphore in a table. The task argument is the port of
// _mach_task_self_ and is not looked at.
#define SEM_BASE 0x5003u
#define SEM_MAX 4096
static sem_t g_sem[SEM_MAX];
static unsigned char g_sem_used[SEM_MAX];
static pthread_mutex_t g_sem_lock = PTHREAD_MUTEX_INITIALIZER;

static sem_t *sem_of(unsigned port) {
  unsigned i = port - SEM_BASE;
  return i < SEM_MAX && g_sem_used[i] ? &g_sem[i] : NULL;
}

int dw_semaphore_create(unsigned task, unsigned *port, int policy, int value) {
  (void)task, (void)policy;
  if (value < 0) return KERN_INVALID_ARGUMENT;
  int r = KERN_RESOURCE_SHORTAGE;
  pthread_mutex_lock(&g_sem_lock);
  for (unsigned i = 0; i < SEM_MAX; i++) {
    if (g_sem_used[i]) continue;
    sem_init(&g_sem[i], 0, (unsigned)value);
    g_sem_used[i] = 1;
    *port = SEM_BASE + i;
    r = KERN_SUCCESS;
    break;
  }
  pthread_mutex_unlock(&g_sem_lock);
  return r;
}
DW_FUNCTION("_semaphore_create", dw_semaphore_create);

int dw_semaphore_destroy(unsigned task, unsigned port) {
  (void)task;
  pthread_mutex_lock(&g_sem_lock);
  sem_t *s = sem_of(port);
  if (s) {
    sem_destroy(s);
    g_sem_used[port - SEM_BASE] = 0;
  }
  pthread_mutex_unlock(&g_sem_lock);
  return s ? KERN_SUCCESS : KERN_INVALID_ARGUMENT;
}
DW_FUNCTION("_semaphore_destroy", dw_semaphore_destroy);

int dw_semaphore_signal(unsigned port) {
  sem_t *s = sem_of(port);
  if (!s) return KERN_INVALID_ARGUMENT;
  sem_post(s);
  return KERN_SUCCESS;
}
DW_FUNCTION("_semaphore_signal", dw_semaphore_signal);

int dw_semaphore_wait(unsigned port) {
  sem_t *s = sem_of(port);
  if (!s) return KERN_INVALID_ARGUMENT;
  int saved = errno;
  while (sem_wait(s) && errno == EINTR) {
  }
  errno = saved;  // a Mach call leaves errno alone
  return KERN_SUCCESS;
}
DW_FUNCTION("_semaphore_wait", dw_semaphore_wait);

// mach_port_type: what kind of right a name is. The task's own port and a live semaphore are send rights; any
// other name is none.
int dw_mach_port_type(unsigned task, unsigned name, unsigned *type) {
  (void)task;
  if (name != 0x103 && !sem_of(name)) return KERN_INVALID_NAME;
  *type = 0x10000;  // MACH_PORT_TYPE_SEND
  return KERN_SUCCESS;
}
DW_FUNCTION("_mach_port_type", dw_mach_port_type);

// --- OSAtomic: each returns the new value, or whether the swap was made ---
int32_t dw_OSAtomicAdd32(int32_t amount, volatile int32_t *value) { return __atomic_add_fetch(value, amount, __ATOMIC_SEQ_CST); }
DW_FUNCTION("_OSAtomicAdd32", dw_OSAtomicAdd32);

int32_t dw_OSAtomicIncrement32(volatile int32_t *value) { return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST); }
DW_FUNCTION("_OSAtomicIncrement32", dw_OSAtomicIncrement32);

int32_t dw_OSAtomicDecrement32(volatile int32_t *value) { return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST); }
DW_FUNCTION("_OSAtomicDecrement32", dw_OSAtomicDecrement32);

_Bool dw_OSAtomicCompareAndSwap32(int32_t old, int32_t new, volatile int32_t *value) {
  return __atomic_compare_exchange_n(value, &old, new, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
DW_FUNCTION("_OSAtomicCompareAndSwap32", dw_OSAtomicCompareAndSwap32);

_Bool dw_OSAtomicCompareAndSwap64(int64_t old, int64_t new, volatile int64_t *value) {
  return __atomic_compare_exchange_n(value, &old, new, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
DW_FUNCTION("_OSAtomicCompareAndSwap64", dw_OSAtomicCompareAndSwap64);
