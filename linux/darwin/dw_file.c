// Files and folders: Darwin's flags and struct layouts over the host's calls, and names found whatever their
// letter case. (The Mac app sends the same imports through the runtime's rc_file, where a mod can serve a file;
// that joins here when mods do.)
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <semaphore.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dw.h"

// --- names ---
// macOS's file system takes a name in any letter case, and the game relies on it. A path that is not there as
// written is walked part by part, and a part that is missing is looked for by a name that differs in case alone.
// What is still missing after that (a file about to be made) stays as written.
static int file_log(void) {
  static int on = -1;
  if (on < 0) {
    const char *e = getenv("RS_FILE_LOG");
    on = e && *e && *e != '0';
  }
  return on;
}

const char *dw_path(const char *path, char *buf, size_t n) {
  struct stat st;
  if (!path || !*path || lstat(path, &st) == 0) return path;
  size_t len = strlen(path);
  if (len + 1 > n) return path;
  size_t out = 0;      // buf[0..out) is the part found so far
  const char *p = path;
  int changed = 0;
  if (*p == '/') buf[out++] = *p++;
  while (*p) {
    const char *end = strchrnul(p, '/');
    size_t part = (size_t)(end - p);
    size_t at = out;
    memcpy(buf + out, p, part);
    out += part;
    buf[out] = 0;
    if (part && lstat(buf, &st) != 0) {
      buf[at] = 0;
      DIR *d = opendir(at ? buf : ".");
      const struct dirent *e = NULL;
      if (d) {
        while ((e = readdir(d)))
          if (strlen(e->d_name) == part && !strncasecmp(e->d_name, p, part)) break;
      }
      if (e) {
        memcpy(buf + at, e->d_name, part);
        changed = 1;
      } else {
        memcpy(buf + at, p, part);  // not there in any case: the rest as written
        if (d) closedir(d);
        memcpy(buf + out, end, strlen(end) + 1);
        break;
      }
      closedir(d);
      buf[out] = 0;
    }
    if (*end) buf[out++] = '/';
    buf[out] = 0;
    p = *end ? end + 1 : end;
  }
  if (!changed) return path;
  if (file_log()) fprintf(stderr, "[file] %s is %s here\n", path, buf);
  return buf;
}

// --- open and its flags ---
static int host_open_flags(int darwin) {
  int f = darwin & 3;  // O_RDONLY, O_WRONLY, O_RDWR
  if (darwin & 0x4) f |= O_NONBLOCK;
  if (darwin & 0x8) f |= O_APPEND;
  if (darwin & 0x80) f |= O_SYNC;
  if (darwin & 0x100) f |= O_NOFOLLOW;
  if (darwin & 0x200) f |= O_CREAT;
  if (darwin & 0x400) f |= O_TRUNC;
  if (darwin & 0x800) f |= O_EXCL;
  if (darwin & 0x20000) f |= O_NOCTTY;
  if (darwin & 0x100000) f |= O_DIRECTORY;
  if (darwin & 0x1000000) f |= O_CLOEXEC;
  return f;  // O_SHLOCK and O_EXLOCK (0x10, 0x20), BSD's lock at open, have no counterpart and are left out
}

static int darwin_status_flags(int host) {
  int f = host & 3;
  if (host & O_NONBLOCK) f |= 0x4;
  if (host & O_APPEND) f |= 0x8;
  if (host & O_SYNC) f |= 0x80;
  return f;
}

// The files a mod serves to the game (runtime/rc_file.h): where the program has the mods' runtime, every one of
// the game's file calls goes through it, as on the Mac, and it answers for a served file and hands the rest to the
// system. A test of the layer alone has none of it (the names are weak here), and the calls are the system's.
__attribute__((weak)) int rc_file_open(const char *path, int flags, ...);
__attribute__((weak)) int rc_file_close(int fd);
__attribute__((weak)) ssize_t rc_file_read(int fd, void *buf, size_t n);
__attribute__((weak)) int rc_file_fstat(int fd, struct stat *st);
__attribute__((weak)) int rc_file_stat(const char *path, struct stat *st);
__attribute__((weak)) FILE *rc_file_fopen(const char *path, const char *mode);
__attribute__((weak)) DIR *rc_file_opendir(const char *path);
__attribute__((weak)) struct dirent *rc_file_readdir(DIR *d);
__attribute__((weak)) int rc_file_closedir(DIR *d);

int dw_open(const char *path, int flags, ...) {
  int mode = 0;
  if (flags & 0x200) {  // Darwin's O_CREAT: a third argument
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  char buf[PATH_MAX];
  path = dw_path(path, buf, sizeof buf);
  return rc_file_open ? rc_file_open(path, host_open_flags(flags), mode) : open(path, host_open_flags(flags), mode);
}
DW_FUNCTION("_open", dw_open);

int dw_close(int fd) { return rc_file_close ? rc_file_close(fd) : close(fd); }
DW_FUNCTION("_close", dw_close);

ssize_t dw_read(int fd, void *buf, size_t n) { return rc_file_read ? rc_file_read(fd, buf, n) : read(fd, buf, n); }
DW_FUNCTION("_read", dw_read);

FILE *dw_fopen(const char *path, const char *mode) {
  char buf[PATH_MAX];
  const char *host = dw_path(path, buf, sizeof buf);
  FILE *f = rc_file_fopen ? rc_file_fopen(host, mode) : fopen(host, mode);
  if (!f && file_log()) fprintf(stderr, "[file] fopen %s (%s): %s\n", path, mode, strerror(errno));
  return f;
}
DW_FUNCTION("_fopen", dw_fopen);

// --- stat ---
// Darwin's struct stat with 64-bit inodes (the $INODE64 names), 144 bytes. The mode's bits are the same.
struct dw_timespec {
  int64_t sec, nsec;
};
struct dw_stat {
  int32_t dev;
  uint16_t mode, nlink;
  uint64_t ino;
  uint32_t uid, gid;
  int32_t rdev;
  struct dw_timespec atime, mtime, ctime, birthtime;
  int64_t size, blocks;
  int32_t blksize;
  uint32_t flags, gen;
  int32_t lspare;
  int64_t qspare[2];
};
_Static_assert(sizeof(struct dw_stat) == 144, "Darwin's struct stat");

static void fill_stat(struct dw_stat *out, const struct stat *st) {
  memset(out, 0, sizeof *out);
  out->dev = (int32_t)st->st_dev;
  out->mode = (uint16_t)st->st_mode;
  out->nlink = (uint16_t)(st->st_nlink > 0xffff ? 0xffff : st->st_nlink);
  out->ino = st->st_ino;
  out->uid = st->st_uid;
  out->gid = st->st_gid;
  out->rdev = (int32_t)st->st_rdev;
  out->atime = (struct dw_timespec){st->st_atim.tv_sec, st->st_atim.tv_nsec};
  out->mtime = (struct dw_timespec){st->st_mtim.tv_sec, st->st_mtim.tv_nsec};
  out->ctime = (struct dw_timespec){st->st_ctim.tv_sec, st->st_ctim.tv_nsec};
  out->birthtime = out->mtime;  // Linux's stat has no time of birth
  out->size = st->st_size;
  out->blocks = st->st_blocks;
  out->blksize = (int32_t)st->st_blksize;
}

int dw_stat(const char *path, struct dw_stat *out) {
  char buf[PATH_MAX];
  struct stat st;
  path = dw_path(path, buf, sizeof buf);
  if (rc_file_stat ? rc_file_stat(path, &st) : stat(path, &st)) return -1;
  fill_stat(out, &st);
  return 0;
}
DW_FUNCTION("_stat$INODE64", dw_stat);

int dw_fstat(int fd, struct dw_stat *out) {
  struct stat st;
  if (rc_file_fstat ? rc_file_fstat(fd, &st) : fstat(fd, &st)) return -1;
  fill_stat(out, &st);
  return 0;
}
DW_FUNCTION("_fstat$INODE64", dw_fstat);

// --- folders ---
// Darwin's struct dirent with 64-bit inodes: the name's length beside it, and room for 1,023 bytes of name. The
// game holds the stream as an opaque pointer and passes it to these three alone, so the stream it gets is ours:
// the host's, and the entry in Darwin's form that readdir hands out.
struct dw_dirent {
  uint64_t ino, seekoff;
  uint16_t reclen, namlen;
  uint8_t type;
  char name[1024];
};
_Static_assert(sizeof(struct dw_dirent) == 1048 && __builtin_offsetof(struct dw_dirent, name) == 21, "Darwin's struct dirent");

struct dw_dir {
  DIR *host;
  struct dw_dirent entry;
};

struct dw_dir *dw_opendir(const char *path) {
  char buf[PATH_MAX];
  path = dw_path(path, buf, sizeof buf);
  DIR *host = rc_file_opendir ? rc_file_opendir(path) : opendir(path);
  if (!host) return NULL;
  struct dw_dir *dir = malloc(sizeof *dir);
  if (!dir) {
    rc_file_closedir ? rc_file_closedir(host) : closedir(host);
    errno = ENOMEM;
    return NULL;
  }
  dir->host = host;
  return dir;
}
DW_FUNCTION("_opendir$INODE64", dw_opendir);

struct dw_dirent *dw_readdir(struct dw_dir *dir) {
  const struct dirent *e = rc_file_readdir ? rc_file_readdir(dir->host) : readdir(dir->host);  // (with what a mod adds)
  if (!e) return NULL;
  struct dw_dirent *out = &dir->entry;
  size_t n = strlen(e->d_name);
  if (n > sizeof out->name - 1) n = sizeof out->name - 1;
  out->ino = e->d_ino;
  out->seekoff = (uint64_t)e->d_off;
  out->reclen = sizeof *out;
  out->namlen = (uint16_t)n;
  out->type = e->d_type;  // DT_ values are the same
  if (out->type == DT_UNKNOWN) {  // a file system that does not say: ask
    struct stat st;
    if (!fstatat(dirfd(dir->host), e->d_name, &st, AT_SYMLINK_NOFOLLOW))
      out->type = S_ISDIR(st.st_mode) ? DT_DIR : S_ISREG(st.st_mode) ? DT_REG : S_ISLNK(st.st_mode) ? DT_LNK : DT_UNKNOWN;
  }
  memcpy(out->name, e->d_name, n);
  out->name[n] = 0;
  return out;
}
DW_FUNCTION("_readdir$INODE64", dw_readdir);

int dw_closedir(struct dw_dir *dir) {
  int r = rc_file_closedir ? rc_file_closedir(dir->host) : closedir(dir->host);
  free(dir);
  return r;
}
DW_FUNCTION("_closedir", dw_closedir);

// --- fcntl ---
// Darwin's commands: 0 to 4 as here; then 5 F_GETOWN, 6 F_SETOWN, 7 F_GETLK, 8 F_SETLK, 9 F_SETLKW, and its own
// (45 F_RDAHEAD, 48 F_NOCACHE, 50 F_GETPATH, 51 F_FULLFSYNC).
struct dw_flock {
  int64_t start, len;
  int32_t pid;
  int16_t type, whence;  // type: 1 read, 2 unlock, 3 write
};

int dw_fcntl(int fd, int cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  long arg = va_arg(ap, long);
  va_end(ap);
  switch (cmd) {
    case 0: return fcntl(fd, F_DUPFD, (int)arg);
    case 1: return fcntl(fd, F_GETFD);
    case 2: return fcntl(fd, F_SETFD, (int)arg);
    case 3: {
      int f = fcntl(fd, F_GETFL);
      return f < 0 ? f : darwin_status_flags(f);
    }
    case 4: return fcntl(fd, F_SETFL, host_open_flags((int)arg) & (O_NONBLOCK | O_APPEND | O_SYNC));
    case 7: case 8: case 9: {
      struct dw_flock *l = (struct dw_flock *)arg;
      struct flock h = {.l_type = l->type == 1 ? F_RDLCK : l->type == 3 ? F_WRLCK : F_UNLCK, .l_whence = l->whence,
                        .l_start = l->start, .l_len = l->len, .l_pid = l->pid};
      int r = fcntl(fd, cmd == 7 ? F_GETLK : cmd == 8 ? F_SETLK : F_SETLKW, &h);
      if (!r && cmd == 7) {
        l->type = h.l_type == F_RDLCK ? 1 : h.l_type == F_WRLCK ? 3 : 2;
        l->whence = h.l_whence, l->start = h.l_start, l->len = h.l_len, l->pid = h.l_pid;
      }
      return r;
    }
    case 45: case 48: return 0;  // read-ahead and the cache: hints
    case 50: {                   // F_GETPATH into a buffer of MAXPATHLEN
      char link[64];
      snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
      ssize_t n = readlink(link, (char *)arg, 1023);
      if (n < 0) return -1;
      ((char *)arg)[n] = 0;
      return 0;
    }
    case 51: return fsync(fd);  // F_FULLFSYNC
  }
  dw_log("fcntl command %d (Darwin's number) is not known here", cmd);
  errno = EINVAL;
  return -1;
}
DW_FUNCTION("_fcntl", dw_fcntl);

// --- poll and select ---
// poll: the struct and the low bits are the same; POLLWRNORM is POLLOUT on Darwin (0x4) and its own bit here
// (0x100), which is Darwin's POLLWRBAND (0x200 here).
int dw_poll(struct pollfd *fds, unsigned n, int timeout) {
  for (unsigned i = 0; i < n; i++)
    if (fds[i].events & 0x100) fds[i].events = (short)((fds[i].events & ~0x100) | POLLWRBAND);
  int r = poll(fds, n, timeout);
  for (unsigned i = 0; i < n; i++) {
    short e = fds[i].events, v = fds[i].revents;
    if (e & POLLWRBAND) fds[i].events = (short)((e & ~POLLWRBAND) | 0x100);
    if (v & POLLWRNORM) v = (short)((v & ~POLLWRNORM) | POLLOUT);
    if (v & POLLWRBAND) v = (short)((v & ~POLLWRBAND) | 0x100);
    fds[i].revents = v;
  }
  return r;
}
DW_FUNCTION("_poll", dw_poll);

// select: the sets are the same bits. Darwin's timeval has 4 bytes of microseconds, and is not written back.
struct dw_timeval {
  int64_t sec;
  int32_t usec;
};
int dw_select(int n, fd_set *r, fd_set *w, fd_set *e, const struct dw_timeval *timeout) {
  struct timeval t;
  if (timeout) t.tv_sec = timeout->sec, t.tv_usec = timeout->usec;
  return select(n, r, w, e, timeout ? &t : NULL);
}
DW_FUNCTION("_select$1050", dw_select);

// --- a named semaphore ---
// The flags (O_CREAT is 0x200 on Darwin, O_EXCL 0x800), and failure is -1 on Darwin, 0 here.
sem_t *dw_sem_open(const char *name, int flags, ...) {
  int mode = 0;
  unsigned value = 0;
  if (flags & 0x200) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    value = va_arg(ap, unsigned);
    va_end(ap);
  }
  sem_t *s = sem_open(name, host_open_flags(flags) & (O_CREAT | O_EXCL), mode, value);
  return s == SEM_FAILED ? (sem_t *)-1 : s;
}
DW_FUNCTION("_sem_open", dw_sem_open);

// --- setjmp and longjmp ---
// The game's code calls them as it is, so they are the host's own: _setjmp, which saves no signal mask and writes
// 68 bytes into the 148 the game has for Darwin's jmp_buf, and the longjmp that reads them back.
DW_FUNCTION("_setjmp", _setjmp);
DW_FUNCTION("_longjmp", longjmp);
