// Darwin's <mach-o/dyld.h>, as much of it as the project's shared code asks for (linux/compat/compat.c).
#ifndef RS_COMPAT_MACH_O_DYLD_H
#define RS_COMPAT_MACH_O_DYLD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The running program's path: 0, or -1 with *size the size needed. The shared code finds the game's folder from
// it, by the Mac app's layout (<game>/Rocksmith2014.app/Contents/MacOS/<program>). A Linux program is not in an
// app, so the loader names the game's own program in the game's folder (RS_MAC_EXECUTABLE) and that is what is
// answered; without it, this program's own path.
int _NSGetExecutablePath(char *buf, uint32_t *size);

#ifdef __cplusplus
}
#endif
#endif
