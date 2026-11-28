// Darwin's removefile(3), as much of it as the project's shared code asks for (linux/compat/compat.c): a file,
// or with REMOVEFILE_RECURSIVE a tree.
#ifndef RS_COMPAT_REMOVEFILE_H
#define RS_COMPAT_REMOVEFILE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rs_removefile_state *removefile_state_t;
typedef unsigned removefile_flags_t;

#define REMOVEFILE_RECURSIVE (1u << 0)

int removefile(const char *path, removefile_state_t state, removefile_flags_t flags);

#ifdef __cplusplus
}
#endif
#endif
