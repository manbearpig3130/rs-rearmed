// The recompiler runtime's interface (tools/ws7/recomp/runtime/rc.h) where nothing is translated: x86-64 Linux,
// where the game's own code runs as it is (linux/X86-64.md). The app's native code that touches the game (the
// seam, later the overrides and fixes) is written against rc.h, rc_abi.h and rc_rt.h; these are the same names
// with the meaning they have when the game's registers are the CPU's and its image is at its own address.
// As much of the interface as that code uses so far.
#ifndef RC_DIRECT_RC_H
#define RC_DIRECT_RC_H

#include <stdint.h>

#define RC_DIRECT 1
// An image address as a host address: the same number, as the image is not moved.
#define RC_A(vmaddr) ((uint64_t)(vmaddr))

#endif
