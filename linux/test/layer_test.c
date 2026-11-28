// The Darwin layer by itself: each function called as the game calls it, with Darwin's layouts and numbers.
// `make -C linux check` builds and runs it; it needs no game.
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

int main(void) {
  dw_init();
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
