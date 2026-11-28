// The few macOS functions the project's shared libraries call (shims/steam, ...), for Linux: the headers beside
// this file declare them under their Darwin names, so that the shared sources build unchanged. Each library that
// needs them is built with this file, hidden, so nothing here is seen outside it.
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "copyfile.h"
#include "mach-o/dyld.h"
#include "removefile.h"
#include "sys/sysctl.h"

#ifndef FICLONE
#define FICLONE _IOW(0x94, 9, int)
#endif

static int copy_one(const char *from, const char *to, const struct stat *st) {
  int in = open(from, O_RDONLY | O_CLOEXEC);
  if (in < 0) return -1;
  int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st->st_mode & 07777);
  if (out < 0) {
    int e = errno;
    close(in);
    errno = e;
    return -1;
  }
  int r = 0;
  if (ioctl(out, FICLONE, in) != 0) {  // no shared blocks on this file system, or across two: copy
    off_t left = st->st_size;
    while (left > 0 && r == 0) {
      ssize_t n = copy_file_range(in, NULL, out, NULL, (size_t)left, 0);
      if (n > 0) left -= n;
      else if (n == 0) break;
      else if (errno == EINTR) continue;
      else {  // a kernel or file system without it: by hand, from where both stand
        char buf[1 << 16];
        ssize_t got;
        while ((got = read(in, buf, sizeof buf)) > 0)
          for (ssize_t done = 0, w; done < got; done += w)
            if ((w = write(out, buf + done, (size_t)(got - done))) < 0) {
              r = -1;
              goto done;
            }
        if (got < 0) r = -1;
        break;
      }
    }
  }
done:;
  int e = errno;
  struct timespec times[2] = {st->st_atim, st->st_mtim};
  if (r == 0) futimens(out, times);
  close(in);
  if (close(out) != 0 && r == 0) r = -1, e = errno;
  if (r) errno = e;
  return r;
}

static int copy_tree(const char *from, const char *to) {
  struct stat st;
  if (lstat(from, &st)) return -1;
  if (S_ISLNK(st.st_mode)) {
    char target[PATH_MAX];
    ssize_t n = readlink(from, target, sizeof target - 1);
    if (n < 0) return -1;
    target[n] = 0;
    return symlink(target, to);
  }
  if (!S_ISDIR(st.st_mode)) return S_ISREG(st.st_mode) ? copy_one(from, to, &st) : 0;
  if (mkdir(to, (st.st_mode & 07777) | 0700) && errno != EEXIST) return -1;
  DIR *d = opendir(from);
  if (!d) return -1;
  int r = 0;
  for (struct dirent *e; r == 0 && (e = readdir(d));) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char a[PATH_MAX], b[PATH_MAX];
    if (snprintf(a, sizeof a, "%s/%s", from, e->d_name) >= (int)sizeof a ||
        snprintf(b, sizeof b, "%s/%s", to, e->d_name) >= (int)sizeof b) {
      errno = ENAMETOOLONG;
      r = -1;
    } else {
      r = copy_tree(a, b);
    }
  }
  int e = errno;
  closedir(d);
  errno = e;
  return r;
}

int copyfile(const char *from, const char *to, copyfile_state_t state, copyfile_flags_t flags) {
  (void)state;
  if (flags & COPYFILE_RECURSIVE) return copy_tree(from, to);
  struct stat st;
  if (stat(from, &st)) return -1;
  if (!S_ISREG(st.st_mode)) {
    errno = S_ISDIR(st.st_mode) ? EISDIR : EINVAL;
    return -1;
  }
  return copy_one(from, to, &st);
}

static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *ftw) {
  (void)st, (void)type, (void)ftw;
  return remove(path);
}

int removefile(const char *path, removefile_state_t state, removefile_flags_t flags) {
  (void)state;
  if (flags & REMOVEFILE_RECURSIVE) return nftw(path, remove_entry, 32, FTW_DEPTH | FTW_PHYS) ? -1 : 0;
  return remove(path);
}

int _NSGetExecutablePath(char *buf, uint32_t *size) {
  char own[PATH_MAX];
  const char *path = getenv("RS_MAC_EXECUTABLE");
  if (!path || !*path) {
    ssize_t n = readlink("/proc/self/exe", own, sizeof own - 1);
    if (n < 0) return -1;
    own[n] = 0;
    path = own;
  }
  size_t need = strlen(path) + 1;
  if (need > *size) {
    *size = (uint32_t)need;
    return -1;
  }
  memcpy(buf, path, need);
  return 0;
}

int sysctl(int *name, unsigned namelen, void *old, size_t *oldlen, void *new_value, size_t newlen) {
  (void)new_value, (void)newlen;
  if (namelen == 2 && name[0] == CTL_KERN && name[1] == KERN_BOOTTIME && old && oldlen && *oldlen >= sizeof(struct timeval)) {
    struct timespec now, up;
    clock_gettime(CLOCK_REALTIME, &now);
    clock_gettime(CLOCK_BOOTTIME, &up);
    struct timeval boot = {now.tv_sec - up.tv_sec, 0};
    memcpy(old, &boot, sizeof boot);
    *oldlen = sizeof boot;
    return 0;
  }
  errno = ENOENT;
  return -1;
}

// kern.maxfilesperproc: the hard limit of open files, which is what a process may raise its own to.
#include <sys/resource.h>
int sysctlbyname(const char *name, void *old, size_t *oldlen, void *new_value, size_t newlen) {
  (void)new_value, (void)newlen;
  if (name && !strcmp(name, "kern.maxfilesperproc") && old && oldlen && *oldlen >= sizeof(int)) {
    struct rlimit r;
    if (getrlimit(RLIMIT_NOFILE, &r)) return -1;
    int most = r.rlim_max > 0x7fffffff ? 0x7fffffff : (int)r.rlim_max;
    memcpy(old, &most, sizeof most);
    *oldlen = sizeof most;
    return 0;
  }
  errno = ENOENT;
  return -1;
}

// --- libraries by their Mac names (rs_linux.h) ---
// This file's own library's folder, where the project's other libraries are.
static int beside(const char *file, char *out, size_t n) {
  Dl_info info;
  if (!dladdr((void *)beside, &info) || !info.dli_fname) return 0;
  const char *slash = strrchr(info.dli_fname, '/');
  int dir = slash ? (int)(slash - info.dli_fname) : 1;
  return snprintf(out, n, "%.*s/%s", dir, slash ? info.dli_fname : ".", file) < (int)n;
}

void *rs_compat_dlopen(const char *path, int flags) {
  char ours[PATH_MAX];
  if (path && strstr(path, "/OpenGL.framework/") && beside("libRSSystemGL.so", ours, sizeof ours)) return dlopen(ours, flags);
  return dlopen(path, flags);
}

void *rs_compat_dlsym(void *handle, const char *name) {
  void *p = dlsym(handle, name);
  if (p || handle != RTLD_DEFAULT) return p;
  char ours[PATH_MAX];
  void *gl = beside("libRSOpenGL.so", ours, sizeof ours) ? dlopen(ours, RTLD_NOW | RTLD_NOLOAD) : NULL;
  return gl ? dlsym(gl, name) : NULL;
}

