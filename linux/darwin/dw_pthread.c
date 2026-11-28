// pthreads and the scheduler. glibc's objects are no larger than Darwin's, so the image's own storage holds them
// and most calls are the host's (`same`). Here are the ones where a number or a size differs.
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

#include "dw.h"

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

// macOS answers 15 and 47 for every policy, and the game places its threads' priorities between the two. Linux's
// range for the default policy is 0 to 0.
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
