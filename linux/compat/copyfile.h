// Darwin's copyfile(3), as much of it as the project's shared code asks for (linux/compat/compat.c): a file or,
// with COPYFILE_RECURSIVE, a tree, with its mode and times. COPYFILE_CLONE shares the blocks where the file system
// can (btrfs, XFS) and copies where it cannot, as on APFS.
#ifndef RS_COMPAT_COPYFILE_H
#define RS_COMPAT_COPYFILE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rs_copyfile_state *copyfile_state_t;
typedef unsigned copyfile_flags_t;

#define COPYFILE_ACL (1u << 0)
#define COPYFILE_STAT (1u << 1)
#define COPYFILE_XATTR (1u << 2)
#define COPYFILE_DATA (1u << 3)
#define COPYFILE_SECURITY (COPYFILE_STAT | COPYFILE_ACL)
#define COPYFILE_METADATA (COPYFILE_SECURITY | COPYFILE_XATTR)
#define COPYFILE_ALL (COPYFILE_METADATA | COPYFILE_DATA)
#define COPYFILE_RECURSIVE (1u << 15)
#define COPYFILE_CLONE (1u << 24)

int copyfile(const char *from, const char *to, copyfile_state_t state, copyfile_flags_t flags);

#ifdef __cplusplus
}
#endif
#endif
