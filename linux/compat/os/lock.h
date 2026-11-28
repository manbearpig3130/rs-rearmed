// os_unfair_lock for the project's shared code on Linux: a mutex.
#ifndef RS_COMPAT_OS_LOCK_H
#define RS_COMPAT_OS_LOCK_H

#include <pthread.h>

typedef pthread_mutex_t os_unfair_lock;
#define OS_UNFAIR_LOCK_INIT PTHREAD_MUTEX_INITIALIZER
#define os_unfair_lock_lock(l) pthread_mutex_lock(l)
#define os_unfair_lock_unlock(l) pthread_mutex_unlock(l)

#endif
