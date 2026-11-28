// The Darwin layer by itself: each function called as the game calls it, with Darwin's layouts and numbers.
// `make -C linux check` builds and runs it; it needs no game.
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dw.h"

static int g_failed;

#define CHECK(what)                                                  \
  do {                                                               \
    if (!(what)) {                                                   \
      printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what);       \
      g_failed++;                                                    \
    }                                                                \
  } while (0)

// The layer's functions, by the names the image imports them by.
#define FN(type, symbol) ((type)dw_function(symbol))

static void test_tables(void) {
  size_t nf, nd;
  const dw_entry *f = dw_functions(&nf), *d = dw_objects(&nd);
  CHECK(nf >= 5 && nd >= 5);
  for (size_t i = 0; i < nf; i++) {
    CHECK(f[i].symbol[0] == '_' && f[i].at);
    for (size_t k = 0; k < i; k++) CHECK(strcmp(f[i].symbol, f[k].symbol) != 0);  // one answer to a name
    CHECK(!dw_data(f[i].symbol));
  }
  for (size_t i = 0; i < nd; i++) CHECK(d[i].symbol[0] == '_' && d[i].at);
  CHECK(!dw_function("_no_such_import") && !dw_data("_no_such_import"));
}

static void test_errno(void) {
  int *(*error)(void) = FN(int *(*)(void), "___error");
  CHECK(error != NULL);
  errno = EAGAIN;
  CHECK(*error() == 35);  // Darwin's EAGAIN
  CHECK(errno == 0);      // moved, not copied
  CHECK(*error() == 35);  // and it stays until the host fails again
  *error() = 0;           // the game clears it before a call it means to check
  CHECK(*error() == 0);
  errno = ETIMEDOUT;
  CHECK(*error() == 60);
  errno = EDEADLK;
  CHECK(*error() == 11);
  errno = ENOENT;
  CHECK(*error() == 2);
  errno = ENOMEDIUM;  // no name on Darwin: EIO
  CHECK(*error() == 5);
  CHECK(dw_errno_from_host(0) == 0 && dw_errno_from_host(EOPNOTSUPP) == 45 && dw_errno_from_host(100000) == 5);
}

static void test_pthread(void) {
  int (*key_create)(unsigned long *, void (*)(void *)) = FN(int (*)(unsigned long *, void (*)(void *)), "_pthread_key_create");
  unsigned long key = ~0ul;
  CHECK(key_create(&key, NULL) == 0);
  CHECK(key >> 32 == 0);  // all 8 bytes of Darwin's key written
  CHECK(pthread_setspecific((pthread_key_t)key, &key) == 0 && pthread_getspecific((pthread_key_t)key) == &key);

  int (*settype)(pthread_mutexattr_t *, int) = FN(int (*)(pthread_mutexattr_t *, int), "_pthread_mutexattr_settype");
  // Darwin's objects, which the game has room for: 16 bytes of attributes and 64 of mutex.
  union { pthread_mutexattr_t host; char darwin[16]; } attr;
  union { pthread_mutex_t host; char darwin[64]; } mutex;
  CHECK(sizeof attr == 16 && sizeof mutex == 64);
  pthread_mutexattr_init(&attr.host);
  CHECK(settype(&attr.host, 2) == 0);  // Darwin's PTHREAD_MUTEX_RECURSIVE
  pthread_mutex_init(&mutex.host, &attr.host);
  CHECK(pthread_mutex_lock(&mutex.host) == 0 && pthread_mutex_lock(&mutex.host) == 0);  // a second lock is granted
  pthread_mutex_unlock(&mutex.host);
  pthread_mutex_unlock(&mutex.host);
  pthread_mutex_destroy(&mutex.host);
  CHECK(settype(&attr.host, 1) == 0);  // Darwin's PTHREAD_MUTEX_ERRORCHECK
  pthread_mutex_init(&mutex.host, &attr.host);
  CHECK(pthread_mutex_lock(&mutex.host) == 0 && pthread_mutex_lock(&mutex.host) == EDEADLK);  // and refused here
  pthread_mutex_unlock(&mutex.host);
  pthread_mutex_destroy(&mutex.host);
  CHECK(settype(&attr.host, 3) == 22);
  pthread_mutexattr_destroy(&attr.host);

  CHECK(FN(int (*)(int), "_sched_get_priority_min")(1) == 15 && FN(int (*)(int), "_sched_get_priority_max")(1) == 47);
}

static void test_objects(void) {
  CHECK(*(FILE **)dw_data("___stderrp") == stderr && *(FILE **)dw_data("___stdoutp") == stdout &&
        *(FILE **)dw_data("___stdinp") == stdin);
  uintptr_t guard = *(uintptr_t *)dw_data("___stack_chk_guard");
  CHECK(guard != 0 && (guard & 0xff) == 0);
  char buf[8] = "1234567";
  FN(void (*)(void *, size_t), "___bzero")(buf, 4);
  CHECK(!memcmp(buf, "\0\0\0\0" "567", 8));
}

static void test_rune(void) {
  // Darwin's layout: the image reads these offsets itself.
  CHECK(offsetof(struct dw_rune_locale, runetype) == 60 && offsetof(struct dw_rune_locale, maplower) == 1084 &&
        offsetof(struct dw_rune_locale, mapupper) == 2108 && offsetof(struct dw_rune_locale, runetype_ext) == 3136 &&
        sizeof(struct dw_rune_locale) == 3208);
  const struct dw_rune_locale *t = dw_data("__DefaultRuneLocale");
  enum { A = 0x100, D = 0x400, L = 0x1000, P = 0x2000, S = 0x4000, U = 0x8000, X = 0x10000 };
  CHECK(!memcmp(t->magic, "RuneMagA", 8));
  CHECK((t->runetype['A'] & (A | U | L | X | 0xff)) == (A | U | X | 10));  // a hex digit keeps its value
  CHECK((t->runetype['z'] & (A | U | L | X)) == (A | L));
  CHECK((t->runetype['7'] & (D | X | A | 0xff)) == (D | X | 7));
  CHECK((t->runetype[' '] & S) && (t->runetype['\n'] & S) && (t->runetype['~'] & P) && !t->runetype[0x80]);
  CHECK(t->maplower['Q'] == 'q' && t->mapupper['q'] == 'Q' && t->maplower['1'] == '1' && t->mapupper[0xe9] == 0xe9);
  int (*maskrune)(int, unsigned long) = FN(int (*)(int, unsigned long), "___maskrune");
  CHECK(maskrune('x', A) == A && maskrune('x', D) == 0 && maskrune(0x3b1, A) == 0);
  CHECK(FN(int (*)(int), "___tolower")('Q') == 'q' && FN(int (*)(int), "___toupper")(0x3b1) == 0x3b1);
}

// A mutex and a condition as Darwin's static initializers leave them: a signature and zeros.
static void test_static_initializers(void) {
  union { pthread_mutex_t host; uint64_t darwin[8]; } m = {.darwin = {0x32AAABA7}};
  union { pthread_cond_t host; uint64_t darwin[6]; } c = {.darwin = {0x3CB0B1BB}};
  int (*lock)(pthread_mutex_t *) = FN(int (*)(pthread_mutex_t *), "_pthread_mutex_lock");
  int (*trylock)(pthread_mutex_t *) = FN(int (*)(pthread_mutex_t *), "_pthread_mutex_trylock");
  int (*unlock)(pthread_mutex_t *) = FN(int (*)(pthread_mutex_t *), "_pthread_mutex_unlock");
  CHECK(lock(&m.host) == 0);
  CHECK(trylock(&m.host) == 16);  // EBUSY: it is a mutex now, and held
  CHECK(unlock(&m.host) == 0 && trylock(&m.host) == 0 && unlock(&m.host) == 0);
  union { pthread_mutex_t host; uint64_t darwin[8]; } r = {.darwin = {0x32AAABA2}};  // the recursive initializer
  CHECK(lock(&r.host) == 0 && lock(&r.host) == 0 && unlock(&r.host) == 0 && unlock(&r.host) == 0);
  CHECK(FN(int (*)(pthread_mutex_t *), "_pthread_mutex_destroy")(&r.host) == 0);

  int (*timedwait)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *) =
      FN(int (*)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *), "_pthread_cond_timedwait");
  struct timespec past = {1, 0};
  lock(&m.host);
  CHECK(timedwait(&c.host, &m.host, &past) == 60);  // Darwin's ETIMEDOUT
  unlock(&m.host);
  CHECK(FN(int (*)(pthread_cond_t *), "_pthread_cond_signal")(&c.host) == 0);
  union { pthread_cond_t host; uint64_t darwin[6]; } c2 = {.darwin = {0x3CB0B1BB}};
  CHECK(FN(int (*)(pthread_cond_t *), "_pthread_cond_broadcast")(&c2.host) == 0 && c2.darwin[0] != 0x3CB0B1BB);
}

static void *thread_body(void *arg) {
  *(unsigned *)arg = FN(unsigned (*)(pthread_t), "_pthread_mach_thread_np")(pthread_self());
  FN(int (*)(const char *), "_pthread_setname_np")("a name longer than fifteen characters");
  return arg;
}

static void test_threads(void) {
  union { pthread_attr_t host; char darwin[64]; } attr;
  CHECK(sizeof attr == 64);
  pthread_attr_init(&attr.host);
  CHECK(FN(int (*)(pthread_attr_t *, int), "_pthread_attr_setdetachstate")(&attr.host, 1) == 0);  // Darwin's joinable
  CHECK(FN(int (*)(pthread_attr_t *, int), "_pthread_attr_setschedpolicy")(&attr.host, 4) == 0);  // Darwin's SCHED_FIFO
  CHECK(FN(int (*)(pthread_attr_t *, int), "_pthread_attr_setinheritsched")(&attr.host, 2) == 0);
  CHECK(FN(int (*)(pthread_attr_t *, int), "_pthread_attr_setscope")(&attr.host, 1) == 0);
  pthread_t t;
  unsigned id = 0;
  void *result = NULL;
  // a thread asked for with a real-time policy starts all the same
  CHECK(FN(int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *), "_pthread_create")(&t, &attr.host, thread_body, &id) == 0);
  CHECK(pthread_join(t, &result) == 0 && result == &id && id != 0);
  CHECK(id != FN(unsigned (*)(pthread_t), "_pthread_mach_thread_np")(pthread_self()));
  CHECK(FN(int (*)(pthread_attr_t *, int), "_pthread_attr_setdetachstate")(&attr.host, 7) == 22);
  pthread_attr_destroy(&attr.host);
  int policy = 0, priority[2] = {0, 0}, want[2] = {40, 0};
  CHECK(FN(int (*)(pthread_t, int, const int *), "_pthread_setschedparam")(pthread_self(), 2, want) == 0);
  CHECK(FN(int (*)(pthread_t, int *, int *), "_pthread_getschedparam")(pthread_self(), &policy, priority) == 0);
  CHECK(policy == 2 && priority[0] == 40);  // read back as the game set them
}

static void test_mach(void) {
  uint64_t (*now)(void) = FN(uint64_t (*)(void), "_mach_absolute_time");
  uint32_t timebase[2] = {0, 0};
  CHECK(FN(int (*)(uint32_t *), "_mach_timebase_info")(timebase) == 0 && timebase[0] == 1 && timebase[1] == 1);
  uint64_t a = now(), b = now();
  CHECK(a > 0 && b >= a && b - a < 1000000000ull);
  unsigned sem = 0, other = 0, type = 0;
  int (*create)(unsigned, unsigned *, int, int) = FN(int (*)(unsigned, unsigned *, int, int), "_semaphore_create");
  int (*sem_signal)(unsigned) = FN(int (*)(unsigned), "_semaphore_signal");
  int (*sem_wait)(unsigned) = FN(int (*)(unsigned), "_semaphore_wait");
  int (*destroy)(unsigned, unsigned) = FN(int (*)(unsigned, unsigned), "_semaphore_destroy");
  CHECK(create(0x103, &sem, 0, 1) == 0 && create(0x103, &other, 0, 0) == 0 && sem != 0 && sem != other);
  CHECK(sem_wait(sem) == 0);  // its first unit
  CHECK(sem_signal(sem) == 0 && sem_wait(sem) == 0);
  CHECK(FN(int (*)(unsigned, unsigned, unsigned *), "_mach_port_type")(0x103, sem, &type) == 0 && type == 0x10000);
  CHECK(destroy(0x103, sem) == 0 && destroy(0x103, sem) == 4 && sem_signal(sem) == 4);  // KERN_INVALID_ARGUMENT
  CHECK(FN(int (*)(unsigned, unsigned, unsigned *), "_mach_port_type")(0x103, sem, &type) == 15);
  destroy(0x103, other);

  int32_t v = 5;
  int64_t w = 7;
  CHECK(FN(int32_t (*)(int32_t, int32_t *), "_OSAtomicAdd32")(-2, &v) == 3 && v == 3);
  CHECK(FN(int32_t (*)(int32_t *), "_OSAtomicIncrement32")(&v) == 4 && FN(int32_t (*)(int32_t *), "_OSAtomicDecrement32")(&v) == 3);
  CHECK(FN(_Bool (*)(int32_t, int32_t, int32_t *), "_OSAtomicCompareAndSwap32")(3, 9, &v) && v == 9);
  CHECK(!FN(_Bool (*)(int32_t, int32_t, int32_t *), "_OSAtomicCompareAndSwap32")(3, 1, &v) && v == 9);
  CHECK(FN(_Bool (*)(int64_t, int64_t, int64_t *), "_OSAtomicCompareAndSwap64")(7, 1ll << 40, &w) && w == 1ll << 40);
}

static void test_sysctl(void) {
  int (*byname)(const char *, void *, size_t *, void *, size_t) =
      FN(int (*)(const char *, void *, size_t *, void *, size_t), "_sysctlbyname");
  int (*numbered)(const int *, unsigned, void *, size_t *, void *, size_t) =
      FN(int (*)(const int *, unsigned, void *, size_t *, void *, size_t), "_sysctl");
  int64_t mem = 0;
  int32_t cpus = 0, sse2 = -1, hz32 = 0;
  char os[16] = "", machine[2];
  size_t n = sizeof mem;
  CHECK(byname("hw.memsize", &mem, &n, NULL, 0) == 0 && n == 8 && mem > (1ll << 26));
  n = sizeof cpus;
  CHECK(byname("hw.logicalcpu", &cpus, &n, NULL, 0) == 0 && n == 4 && cpus >= 1);
  n = sizeof sse2;
  CHECK(byname("hw.optional.sse2", &sse2, &n, NULL, 0) == 0 && sse2 == 1);
  n = sizeof os;
  CHECK(byname("kern.ostype", os, &n, NULL, 0) == 0 && n == 7 && !strcmp(os, "Darwin"));
  n = 0;
  CHECK(byname("hw.machine", NULL, &n, NULL, 0) == 0 && n == 7);  // the size alone
  n = sizeof machine;
  errno = 0;
  CHECK(byname("hw.machine", machine, &n, NULL, 0) == -1 && errno == ENOMEM && !memcmp(machine, "x8", 2));
  errno = 0;
  CHECK(byname("hw.no.such.name", &mem, &n, NULL, 0) == -1 && errno == ENOENT);
  int mib[2] = {6, 15};  // CTL_HW, HW_CPU_FREQ: asked by the engine into four bytes
  n = sizeof hz32;
  CHECK(numbered(mib, 2, &hz32, &n, NULL, 0) == 0 && n == 4 && hz32 != 0);
}

static void test_libc(void) {
  char buf[40];
  memset(buf, 'x', sizeof buf);
  FN(void (*)(void *, const void *, size_t), "_memset_pattern16")(buf, "0123456789abcdef", 20);
  CHECK(!memcmp(buf, "0123456789abcdef0123x", 21));
  struct float2 { float sin, cos; };
  typedef struct float2 (*sincos_fn)(float);
  struct float2 sc = FN(sincos_fn, "___sincosf_stret")(0.0f);
  CHECK(sc.sin == 0.0f && sc.cos == 1.0f);
  CHECK(FN(double (*)(double), "___exp10")(3.0) == 1000.0 && FN(int (*)(void), "____mb_cur_max")() == 1);
  CHECK(!strcmp(FN(char *(*)(int, const char *), "_setlocale")(0, ""), "C"));
  CHECK(!strcmp(FN(char *(*)(int), "_strerror")(35), strerror(EAGAIN)));   // Darwin's EAGAIN
  CHECK(!strcmp(FN(char *(*)(int), "_strerror")(60), strerror(ETIMEDOUT)));
  CHECK(!strcmp(FN(char *(*)(int), "_strerror")(5), strerror(EIO)));
  CHECK(strstr(FN(char *(*)(int), "_strerror")(86), "86") != NULL);        // EBADARCH: Darwin's alone
  CHECK(FN(int (*)(const char *), "_system")("true") == -1);
  errno = 0;
  CHECK(FN(int (*)(int, void *), "_tcgetattr")(0, buf) == -1 && errno == ENOTTY);
  // signals: Darwin's SIGBUS (10) is a fault, taken and not installed; SIGPIPE can be ignored; no handler is run
  CHECK(FN(void *(*)(int, void *), "_signal")(10, (void *)thread_body) == NULL);
  CHECK(FN(void *(*)(int, void *), "_signal")(13, (void *)1) == NULL);
  struct darwin_sigaction { void *handler; uint32_t mask; int flags; } act = {(void *)thread_body, 0, 0}, old = {(void *)1, 1, 1};
  CHECK(FN(int (*)(int, const void *, void *), "_sigaction")(30, &act, &old) == 0 && old.handler == NULL);
  CHECK(FN(void *(*)(int, void *), "_signal")(99, NULL) == (void *)-1);
}

// The network is refused, and nothing is opened.
static void test_net(void) {
  errno = 0;
  CHECK(FN(int (*)(int, int, int), "_socket")(2, 1, 0) == -1 && errno == EAFNOSUPPORT);
  CHECK(FN(void *(*)(const char *), "_gethostbyname")("localhost") == NULL);
  void *list = &list;
  CHECK(FN(int (*)(void **), "_getifaddrs")(&list) == 0 && list == NULL);
  CHECK(FN(int (*)(int, const void *, unsigned), "_connect")(3, &list, 16) == -1 && errno == EBADF);
}

static void test_files(void) {
  char dir[] = "/tmp/dw-layer-test-XXXXXX", path[256], buf[512];
  if (!mkdtemp(dir)) {
    CHECK(!"mkdtemp");
    return;
  }
  // a file under mixed-case names, asked for as the game asks: in lower case
  snprintf(path, sizeof path, "%s/Mixed Case", dir);
  CHECK(mkdir(path, 0700) == 0);
  snprintf(path, sizeof path, "%s/Mixed Case/Song.PSARC", dir);
  int (*open_)(const char *, int, ...) = FN(int (*)(const char *, int, ...), "_open");
  int fd = open_(path, 0x200 | 0x800 | 1, 0600);  // Darwin's O_CREAT | O_EXCL | O_WRONLY
  CHECK(fd >= 0 && write(fd, "hello", 5) == 5);
  CHECK(open_(path, 0x200 | 0x800 | 1, 0600) == -1 && errno == EEXIST);
  FN(int (*)(int), "_close")(fd);
  snprintf(path, sizeof path, "%s/mixed case/song.psarc", dir);
  CHECK(dw_path(path, buf, sizeof buf) == buf && strstr(buf, "/Mixed Case/Song.PSARC"));
  fd = open_(path, 0);
  char got[8] = "";
  CHECK(fd >= 0 && FN(ssize_t (*)(int, void *, size_t), "_read")(fd, got, 8) == 5 && !memcmp(got, "hello", 5));

  // Darwin's struct stat: the size at 96, the mode at 4
  unsigned char st[144];
  memset(st, 0xee, sizeof st);
  CHECK(FN(int (*)(const char *, void *), "_stat$INODE64")(path, st) == 0);
  CHECK(*(int64_t *)(st + 96) == 5 && (*(uint16_t *)(st + 4) & 0170000) == 0100000);
  memset(st, 0xee, sizeof st);
  CHECK(FN(int (*)(int, void *), "_fstat$INODE64")(fd, st) == 0 && *(int64_t *)(st + 96) == 5);
  CHECK(FN(int (*)(int, int, ...), "_fcntl")(fd, 3) == 0);       // F_GETFL: read only
  CHECK(FN(int (*)(int, int, ...), "_fcntl")(fd, 4, 0x4) == 0);  // F_SETFL, Darwin's O_NONBLOCK
  CHECK(FN(int (*)(int, int, ...), "_fcntl")(fd, 3) == 0x4);
  close(fd);
  FILE *f = FN(FILE *(*)(const char *, const char *), "_fopen")(path, "rb");
  CHECK(f != NULL);
  if (f) fclose(f);
  snprintf(path, sizeof path, "%s/mixed case/not there.txt", dir);
  CHECK(strstr(dw_path(path, buf, sizeof buf), "/Mixed Case/not there.txt") != NULL);  // the folder found, the file as written

  // Darwin's struct dirent: the name's length at 18, the type at 20, the name at 21
  snprintf(path, sizeof path, "%s/MIXED CASE", dir);
  void *d = FN(void *(*)(const char *), "_opendir$INODE64")(path);
  CHECK(d != NULL);
  int found = 0;
  for (unsigned char *e; d && (e = FN(void *(*)(void *), "_readdir$INODE64")(d));)
    if (!strcmp((char *)e + 21, "Song.PSARC")) found += *(uint16_t *)(e + 18) == 10 && e[20] == 8;  // DT_REG
  CHECK(found == 1);
  if (d) CHECK(FN(int (*)(void *), "_closedir")(d) == 0);

  struct darwin_timeval { int64_t sec; int32_t usec; } quick = {0, 1000};
  CHECK(FN(int (*)(int, void *, void *, void *, const void *), "_select$1050")(0, NULL, NULL, NULL, &quick) == 0);
  snprintf(path, sizeof path, "%s/Mixed Case/Song.PSARC", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/Mixed Case", dir);
  rmdir(path);
  rmdir(dir);
}

// CoreFoundation: the image's constant strings are {class, flags, characters, length} compiled into it.
static void test_cf(void) {
  struct constant { void *isa; uint64_t flags; const void *chars; long length; };
  struct constant hello = {dw_data("___CFConstantStringClassReference"), 0x7c8, "Hello", 5};
  static const uint16_t wide[] = {'H', 'e', 'l', 'l', 'o', 0x20ac};  // and a euro sign
  struct constant hello_wide = {hello.isa, 0x7d0, wide, 6};
  long (*length)(const void *) = FN(long (*)(const void *), "_CFStringGetLength");
  unsigned char (*cstring)(const void *, char *, long, uint32_t) = FN(unsigned char (*)(const void *, char *, long, uint32_t), "_CFStringGetCString");
  long (*compare)(const void *, const void *, unsigned long) = FN(long (*)(const void *, const void *, unsigned long), "_CFStringCompare");
  void (*release)(void *) = FN(void (*)(void *), "_CFRelease");
  char buf[64];
  CHECK(length(&hello) == 5 && length(&hello_wide) == 6);
  CHECK(cstring(&hello, buf, sizeof buf, 0x08000100) && !strcmp(buf, "Hello"));
  CHECK(cstring(&hello_wide, buf, sizeof buf, 0x08000100) && !strcmp(buf, "Hello\xe2\x82\xac"));
  CHECK(!cstring(&hello_wide, buf, sizeof buf, 0x600));  // the euro sign is no ASCII
  CHECK(!cstring(&hello, buf, 3, 0x08000100));           // no room
  CHECK(FN(const char *(*)(const void *, uint32_t), "_CFStringGetCStringPtr")(&hello, 0x600) == (const char *)hello.chars);
  CHECK(FN(const char *(*)(const void *, uint32_t), "_CFStringGetCStringPtr")(&hello_wide, 0x600) == NULL);
  release(&hello);  // the image's own: nothing happens
  CHECK(hello.length == 5);

  // Wwise's conversion: four-byte characters in, UTF-8 out
  static const uint32_t utf32[] = {'A', 0xe9, 0x1f3b8};
  void *made = FN(void *(*)(void *, const void *, long, uint32_t, unsigned char), "_CFStringCreateWithBytes")(NULL, utf32, 12, 0x1c000100, 0);
  CHECK(made && length(made) == 4);  // the guitar is two UTF-16 units
  CHECK(cstring(made, buf, sizeof buf, 0x08000100) && !strcmp(buf, "A\xc3\xa9\xf0\x9f\x8e\xb8"));
  struct range { long location, length; } all = {0, 4};
  long used = 0;
  uint32_t back[4] = {0};
  long (*bytes)(const void *, struct range, uint32_t, uint8_t, unsigned char, void *, long, long *) =
      FN(long (*)(const void *, struct range, uint32_t, uint8_t, unsigned char, void *, long, long *), "_CFStringGetBytes");
  CHECK(bytes(made, all, 0x1c000100, 0, 0, back, sizeof back, &used) == 4 && used == 12 && !memcmp(back, utf32, 12));
  CHECK(bytes(made, all, 0x08000100, 0, 0, NULL, 0, &used) == 4 && used == 7);  // the size alone
  CHECK(bytes(made, all, 0x600, '?', 0, buf, sizeof buf, &used) == 4 && used == 3 && !memcmp(buf, "A??", 3));
  void *same = FN(void *(*)(void *, const void *, long, uint32_t, unsigned char), "_CFStringCreateWithBytes")(NULL, "hello", 5, 0x08000100, 0);
  CHECK(compare(&hello, same, 0) == -1 && compare(&hello, same, 1) == 0 && compare(same, &hello_wide, 1) == -1);
  release(made);
  release(same);

  void *uuid = FN(void *(*)(void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t,
                            uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t), "_CFUUIDCreateWithBytes")(
      NULL, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);
  void *text = FN(void *(*)(void *, const void *), "_CFUUIDCreateString")(NULL, uuid);
  CHECK(cstring(text, buf, sizeof buf, 0x600) && !strcmp(buf, "01234567-89AB-CDEF-1032-547698BADCFE"));
  release(text);
  release(uuid);

  void *locale = FN(void *(*)(void), "_CFLocaleCopyCurrent")();
  const void *country = FN(const void *(*)(const void *, const void *), "_CFLocaleGetValue")(locale, *(void **)dw_data("_kCFLocaleCountryCode"));
  CHECK(cstring(country, buf, sizeof buf, 0x600) && !strcmp(buf, "US"));
  release(locale);
  CHECK(FN(double (*)(void), "_CFAbsoluteTimeGetCurrent")() > 7.8e8);  // after 2025
}

// The display's modes as the renderer lists them, and IOKit's empty answer.
static void test_display(void) {
  const int modes[][2] = {{1280, 720}, {2560, 1440}};
  dw_display_set_modes(modes, 2);
  unsigned display = FN(unsigned (*)(void), "_CGMainDisplayID")();
  CHECK(FN(unsigned (*)(unsigned), "_CGDisplayIDToOpenGLDisplayMask")(display) == 1);
  void *list = FN(void *(*)(unsigned, const void *), "_CGDisplayCopyAllDisplayModes")(display, NULL);
  CHECK(FN(long (*)(const void *), "_CFArrayGetCount")(list) == 2);
  const void *mode = FN(const void *(*)(const void *, long), "_CFArrayGetValueAtIndex")(list, 1);
  CHECK(FN(size_t (*)(const void *), "_CGDisplayModeGetWidth")(mode) == 2560 && FN(size_t (*)(const void *), "_CGDisplayModeGetHeight")(mode) == 1440);
  CHECK(FN(const void *(*)(const void *, long), "_CFArrayGetValueAtIndex")(list, 2) == NULL);
  void *encoding = FN(void *(*)(const void *), "_CGDisplayModeCopyPixelEncoding")(mode);
  char buf[40];
  CHECK(FN(unsigned char (*)(const void *, char *, long, uint32_t), "_CFStringGetCString")(encoding, buf, sizeof buf, 0x600) &&
        !strcmp(buf, "--------RRRRRRRRGGGGGGGGBBBBBBBB"));
  FN(void (*)(void *), "_CFRelease")(encoding);
  FN(void (*)(void *), "_CFRelease")(list);

  unsigned iterator = 7;
  void *matching = FN(void *(*)(const char *), "_IOServiceMatching")("IOEthernetInterface");
  CHECK(matching && FN(int (*)(unsigned, void *, unsigned *), "_IOServiceGetMatchingServices")(0, matching, &iterator) == 0);
  CHECK(iterator == 0 && FN(unsigned (*)(unsigned), "_IOIteratorNext")(iterator) == 0);
}

int main(void) {
  setenv("RS_DARWIN_LOG", "0", 1);  // what the layer refuses below is refused on purpose
  dw_init();
  test_static_initializers();
  test_threads();
  test_mach();
  test_sysctl();
  test_libc();
  test_net();
  test_files();
  test_cf();
  test_display();
  test_tables();
  test_errno();
  test_pthread();
  test_objects();
  test_rune();
  if (g_failed) {
    printf("the Darwin layer: %d checks failed\n", g_failed);
    return 1;
  }
  size_t nf, nd;
  dw_functions(&nf);
  dw_objects(&nd);
  printf("the Darwin layer: %zu functions and %zu objects, all checks passed\n", nf, nd);
  return 0;
}
