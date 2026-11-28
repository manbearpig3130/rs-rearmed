// Darwin's <sys/sysctl.h>, as much of it as the project's shared code asks for (linux/compat/compat.c): the time
// of boot. (glibc has no sysctl of its own since 2.32.)
#ifndef RS_COMPAT_SYS_SYSCTL_H
#define RS_COMPAT_SYS_SYSCTL_H

#include <stddef.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CTL_KERN 1
#define KERN_BOOTTIME 21

// {CTL_KERN, KERN_BOOTTIME} into a struct timeval; any other name fails with ENOENT.
int sysctl(int *name, unsigned namelen, void *old, size_t *oldlen, void *new_value, size_t newlen);

#ifdef __cplusplus
}
#endif
#endif
