// CGL's current context. One prototype a line: the wrapper table's generator reads this file's text.
#ifndef RS_COMPAT_OPENGL_CGLCURRENT_H
#define RS_COMPAT_OPENGL_CGLCURRENT_H

#include "CGLTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

extern CGLError CGLSetCurrentContext(CGLContextObj ctx);
extern CGLContextObj CGLGetCurrentContext(void);

#ifdef __cplusplus
}
#endif
#endif
