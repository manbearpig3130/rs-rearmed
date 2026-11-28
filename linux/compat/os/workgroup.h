// The types rs_audio_host.h names for a device's audio workgroup, which is macOS's: Linux has no call for them.
#ifndef RS_COMPAT_OS_WORKGROUP_H
#define RS_COMPAT_OS_WORKGROUP_H

typedef void *os_workgroup_t;
typedef struct {
  int unused;
} os_workgroup_join_token_s;

#endif
