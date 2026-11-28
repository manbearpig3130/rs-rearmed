// The game's names (tools/ws7/recomp/runtime/rc_abi.h) where nothing is translated. The values are the Mac target's
// tables, as on every host: rc_target_names.h, which tools/targets/targets.py writes.
#ifndef RC_DIRECT_RC_ABI_H
#define RC_DIRECT_RC_ABI_H

#include "rc.h"
#include "rc_target_names.h"

#define RC_ADDR(name) RC_ADDR_##name
#define RC_OFF(name) RC_OFF_##name
#define RC_SLOT(name) RC_OFF_##name##_slot
#define RC_GA(name) RC_A(RC_ADDR_##name)
#define RC_HOST(g) ((void *)(uintptr_t)(g))
#define RC_GUEST(h) ((uint64_t)(uintptr_t)(h))

#endif
