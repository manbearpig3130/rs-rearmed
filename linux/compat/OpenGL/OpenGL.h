// Apple's <OpenGL/OpenGL.h> for Linux: the CGL calls the project's shared code makes, answered over EGL by
// linux/gl/system_gl.c. One prototype a line: the wrapper table's generator reads this file's text.
#ifndef RS_COMPAT_OPENGL_OPENGL_H
#define RS_COMPAT_OPENGL_OPENGL_H

#include "CGLCurrent.h"
#include "CGLDevice.h"
#include "CGLTypes.h"
#include "gltypes.h"

#ifdef __cplusplus
extern "C" {
#endif

extern CGLError CGLChoosePixelFormat(const CGLPixelFormatAttribute *attribs, CGLPixelFormatObj *pix, GLint *npix);
extern CGLError CGLDestroyPixelFormat(CGLPixelFormatObj pix);
extern CGLError CGLCreateContext(CGLPixelFormatObj pix, CGLContextObj share, CGLContextObj *ctx);
extern CGLError CGLDestroyContext(CGLContextObj ctx);
extern CGLError CGLLockContext(CGLContextObj ctx);
extern CGLError CGLUnlockContext(CGLContextObj ctx);
extern CGLError CGLEnable(CGLContextObj ctx, CGLContextEnable pname);
extern CGLError CGLDisable(CGLContextObj ctx, CGLContextEnable pname);
extern const char *CGLErrorString(CGLError error);

#ifdef __cplusplus
}
#endif
#endif
