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
  pthread_attr_setstacksize(&attr.host, 32768);  // and with a stack too small for this host: it gets a larger one
  // a thread asked for with a real-time policy starts all the same
  CHECK(FN(int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *), "_pthread_create")(&t, &attr.host, thread_body, &id) == 0);
  CHECK(pthread_join(t, &result) == 0 && result == &id && id != 0);
  CHECK(FN(int (*)(pthread_t, void **), "_pthread_join")((pthread_t)0, NULL) == 3);  // no such thread: ESRCH, not a fault
  CHECK(FN(int (*)(pthread_t), "_pthread_detach")((pthread_t)0) == 3);
  CHECK(id != FN(unsigned (*)(pthread_t), "_pthread_mach_thread_np")(pthread_self()));
  CHECK(FN(unsigned (*)(pthread_t), "_pthread_mach_thread_np")((pthread_t)0) == 0);  // no thread, no port
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

// The 4x4 inverse, called as the game calls it: the matrix by value in memory, the result through a pointer.
static void test_simd(void) {
  typedef struct { float m[16]; } __attribute__((aligned(16))) mat4;
  const mat4 *identity = dw_data("_matrix_identity_float4x4");
  CHECK(identity->m[0] == 1 && identity->m[5] == 1 && identity->m[10] == 1 && identity->m[15] == 1 && identity->m[1] == 0);
  mat4 a = {{2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 8, 0, 1, 2, 3, 1}};  // a scale with a translation in the last column
  mat4 r = FN(mat4 (*)(mat4), "___invert_f4")(a);
  CHECK(r.m[0] == 0.5f && r.m[5] == 0.25f && r.m[10] == 0.125f && r.m[12] == -0.5f && r.m[13] == -0.5f && r.m[14] == -0.375f && r.m[15] == 1);
  mat4 back = FN(mat4 (*)(mat4), "___invert_f4")(r);
  CHECK(!memcmp(&back, &a, sizeof a));
}

// The audio devices and the graph, silent: the game's callback is called on the clock with buffers in its format.
static int g_render_calls, g_render_shape_ok;

static int render(void *ref, uint32_t *flags, const void *when, uint32_t bus, uint32_t frames, void *data) {
  struct list { uint32_t count; struct { uint32_t channels, bytes; void *data; } buffers[2]; } *l = data;
  (void)flags, (void)when;
  g_render_shape_ok = ref == &g_render_calls && bus == 0 && frames == 256 && l->count == 2 && l->buffers[0].channels == 1 &&
                      l->buffers[0].bytes == 256 * 4 && l->buffers[1].data != NULL && l->buffers[1].data != l->buffers[0].data;
  __atomic_add_fetch(&g_render_calls, 1, __ATOMIC_SEQ_CST);
  return 0;
}

// A backend in the layer's place of the silent clock: it is started with the graph and given the function that
// fetches the game's sound, as two channels.
static dw_audio_render g_backend_render;
static void *g_backend_ctx;
static int g_backend_started, g_backend_stopped;

static int backend_start(double rate, uint32_t frames, dw_audio_render render, void *ctx) {
  g_backend_render = render, g_backend_ctx = ctx;
  g_backend_started += rate == 48000.0 && frames == 256;
  return 0;
}
static void backend_stop(void) { g_backend_stopped++; }

// The game's callback, filling the buffer of each channel: 0.25 on the left, -0.5 on the right.
static int render_tone(void *ref, uint32_t *flags, const void *when, uint32_t bus, uint32_t frames, void *data) {
  struct list { uint32_t count; struct { uint32_t channels, bytes; void *data; } buffers[2]; } *l = data;
  (void)ref, (void)flags, (void)when, (void)bus;
  for (uint32_t i = 0; i < frames; i++) ((float *)l->buffers[0].data)[i] = 0.25f, ((float *)l->buffers[1].data)[i] = -0.5f;
  return 0;
}

static void test_audio(void) {
  struct address { uint32_t selector, scope, element; };
#define CC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
  int (*get)(uint32_t, const struct address *, uint32_t, const void *, uint32_t *, void *) =
      FN(int (*)(uint32_t, const struct address *, uint32_t, const void *, uint32_t *, void *), "_AudioObjectGetPropertyData");
  uint32_t device = 0, size = sizeof device, input = 7;
  struct address out_device = {CC('d', 'O', 'u', 't'), CC('g', 'l', 'o', 'b'), 0}, in_device = {CC('d', 'I', 'n', ' '), CC('g', 'l', 'o', 'b'), 0};
  CHECK(get(1, &out_device, 0, NULL, &size, &device) == 0 && device != 0 && size == 4);
  CHECK(get(1, &in_device, 0, NULL, &size, &input) == 0 && input == 0);  // no input device
  struct address name = {CC('l', 'n', 'a', 'm'), CC('g', 'l', 'o', 'b'), 0}, rate = {CC('n', 's', 'r', 't'), CC('g', 'l', 'o', 'b'), 0};
  struct address layout = {CC('s', 'l', 'a', 'y'), CC('o', 'u', 't', 'p'), 0}, unknown = {CC('z', 'z', 'z', 'z'), CC('g', 'l', 'o', 'b'), 0};
  void *string = NULL;
  char text[32];
  size = sizeof string;
  CHECK(get(device, &name, 0, NULL, &size, &string) == 0 && string);
  CHECK(FN(unsigned char (*)(const void *, char *, long, uint32_t), "_CFStringGetCString")(string, text, sizeof text, 0x600) && text[0]);
  FN(void (*)(void *), "_CFRelease")(string);
  double hz = 0;
  size = sizeof hz;
  CHECK(get(device, &rate, 0, NULL, &size, &hz) == 0 && hz == 48000.0);
  struct { uint32_t count, channels, bytes; void *data; } channels = {0, 0, 0, NULL};
  size = sizeof channels;
  CHECK(get(device, &layout, 0, NULL, &size, &channels) == 0 && channels.count == 1);
  size = sizeof text;
  CHECK(get(device, &unknown, 0, NULL, &size, text) == (int)CC('w', 'h', 'o', '?'));
  CHECK(get(0x999, &name, 0, NULL, &size, text) == (int)CC('!', 'o', 'b', 'j'));
  CHECK(FN(unsigned char (*)(uint32_t, const struct address *), "_AudioObjectHasProperty")(device, &rate) == 1);
  CHECK(FN(unsigned char (*)(uint32_t, const struct address *), "_AudioObjectHasProperty")(device, &unknown) == 0);

  // the graph the game makes: a mixer it fills through a callback, into the device's output unit
  void *graph = NULL, *mixer_unit = NULL;
  int32_t out_node = 0, mixer_node = 0;
  struct { uint32_t type, subtype, maker, flags, mask; } out_desc = {CC('a', 'u', 'o', 'u'), CC('a', 'h', 'a', 'l'), CC('a', 'p', 'p', 'l'), 0, 0},
      mixer_desc = {CC('a', 'u', 'm', 'x'), CC('m', 'x', 'm', 'x'), CC('a', 'p', 'p', 'l'), 0, 0}, read_back = {0};
  CHECK(FN(int (*)(void **), "_NewAUGraph")(&graph) == 0 && graph);
  int (*add)(void *, const void *, int32_t *) = FN(int (*)(void *, const void *, int32_t *), "_AUGraphAddNode");
  CHECK(add(graph, &out_desc, &out_node) == 0 && add(graph, &mixer_desc, &mixer_node) == 0 && out_node != mixer_node);
  CHECK(FN(int (*)(void *), "_AUGraphOpen")(graph) == 0);
  CHECK(FN(int (*)(void *, int32_t, void *, void **), "_AUGraphNodeInfo")(graph, mixer_node, &read_back, &mixer_unit) == 0 && mixer_unit && read_back.subtype == mixer_desc.subtype);
  struct { double rate; uint32_t format, flags, bpp, fpp, bpf, channels, bits, reserved; } format = {48000.0, CC('l', 'p', 'c', 'm'), 0x29, 4, 1, 4, 2, 32, 0};
  CHECK(FN(int (*)(void *, uint32_t, uint32_t, uint32_t, const void *, uint32_t), "_AudioUnitSetProperty")(mixer_unit, 8, 1, 0, &format, sizeof format) == 0);
  struct { void *proc, *ref; } callback = {(void *)render, &g_render_calls};
  CHECK(FN(int (*)(void *, int32_t, uint32_t, const void *), "_AUGraphSetNodeInputCallback")(graph, mixer_node, 0, &callback) == 0);
  CHECK(FN(int (*)(void *, int32_t, uint32_t, int32_t, uint32_t), "_AUGraphConnectNodeInput")(graph, mixer_node, 0, out_node, 0) == 0);
  CHECK(FN(int (*)(void *), "_AUGraphInitialize")(graph) == 0 && FN(int (*)(void *), "_AUGraphStart")(graph) == 0);
  struct timespec wait = {0, 60000000};  // 60 ms: eleven buffers of 256 frames at 48,000 Hz
  nanosleep(&wait, NULL);
  CHECK(FN(int (*)(void *), "_AUGraphStop")(graph) == 0);
  int calls = g_render_calls;
  CHECK(calls >= 5 && calls <= 20 && g_render_shape_ok);
  nanosleep(&wait, NULL);
  CHECK(g_render_calls == calls);  // stopped
  CHECK(FN(int (*)(void *), "_DisposeAUGraph")(graph) == 0);

  // with a backend: no clock thread; the backend asks for the sound and gets what the game's callback filled
  static const dw_audio_backend backend = {"test", backend_start, backend_stop};
  dw_audio_set_backend(&backend);
  CHECK(FN(int (*)(void **), "_NewAUGraph")(&graph) == 0);
  CHECK(add(graph, &out_desc, &out_node) == 0 && add(graph, &mixer_desc, &mixer_node) == 0);
  CHECK(FN(int (*)(void *, int32_t, void *, void **), "_AUGraphNodeInfo")(graph, mixer_node, NULL, &mixer_unit) == 0);
  CHECK(FN(int (*)(void *, uint32_t, uint32_t, uint32_t, const void *, uint32_t), "_AudioUnitSetProperty")(mixer_unit, 8, 1, 0, &format, sizeof format) == 0);
  struct { void *proc, *ref; } tone = {(void *)render_tone, NULL};
  CHECK(FN(int (*)(void *, int32_t, uint32_t, const void *), "_AUGraphSetNodeInputCallback")(graph, mixer_node, 0, &tone) == 0);
  CHECK(FN(int (*)(void *), "_AUGraphStart")(graph) == 0 && g_backend_started == 1 && g_backend_render);
  float left[256], right[256];
  memset(left, 0x7f, sizeof left);
  memset(right, 0x7f, sizeof right);
  if (g_backend_render) g_backend_render(g_backend_ctx, left, right, 256);
  CHECK(left[0] == 0.25f && left[255] == 0.25f && right[0] == -0.5f && right[255] == -0.5f);
  CHECK(FN(int (*)(void *), "_AUGraphStop")(graph) == 0 && g_backend_stopped == 1);
  CHECK(FN(int (*)(void *), "_DisposeAUGraph")(graph) == 0);
  dw_audio_set_backend(NULL);
#undef CC
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
  test_simd();
  test_audio();
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
