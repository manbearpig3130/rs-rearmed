// CGL's types and numbers, as much of them as the project's shared code names. CGL is macOS's API for OpenGL
// contexts without a window; on Linux its calls are answered over EGL (linux/gl/system_gl.c).
#ifndef RS_COMPAT_OPENGL_CGLTYPES_H
#define RS_COMPAT_OPENGL_CGLTYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _CGLContextObject *CGLContextObj;
typedef struct _CGLPixelFormatObject *CGLPixelFormatObj;
typedef struct _CGLShareGroupRec *CGLShareGroupObj;

typedef enum _CGLPixelFormatAttribute {
  kCGLPFADoubleBuffer = 5,
  kCGLPFAColorSize = 8,
  kCGLPFAAlphaSize = 11,
  kCGLPFADepthSize = 12,
  kCGLPFAStencilSize = 13,
  kCGLPFASampleBuffers = 55,
  kCGLPFASamples = 56,
  kCGLPFANoRecovery = 72,
  kCGLPFAAccelerated = 73,
  kCGLPFAAllowOfflineRenderers = 96,
  kCGLPFAOpenGLProfile = 99
} CGLPixelFormatAttribute;

typedef enum _CGLOpenGLProfile {
  kCGLOGLPVersion_Legacy = 0x1000,
  kCGLOGLPVersion_3_2_Core = 0x3200,
  kCGLOGLPVersion_GL4_Core = 0x4100
} CGLOpenGLProfile;

typedef enum _CGLContextEnable {
  kCGLCESwapRectangle = 201,
  kCGLCESwapLimit = 203,
  kCGLCERasterization = 221,
  kCGLCEStateValidation = 301,
  kCGLCESurfaceBackingSize = 305,
  kCGLCEDisplayListOptimization = 307,
  kCGLCEMPEngine = 313,
  kCGLCECrashOnRemovedFunctions = 316
} CGLContextEnable;

typedef enum _CGLError {
  kCGLNoError = 0,
  kCGLBadAttribute = 10000,
  kCGLBadProperty = 10001,
  kCGLBadPixelFormat = 10002,
  kCGLBadRendererInfo = 10003,
  kCGLBadContext = 10004,
  kCGLBadDrawable = 10005,
  kCGLBadDisplay = 10006,
  kCGLBadState = 10007,
  kCGLBadValue = 10008,
  kCGLBadMatch = 10009,
  kCGLBadEnumeration = 10010,
  kCGLBadOffScreen = 10011,
  kCGLBadFullScreen = 10012,
  kCGLBadWindow = 10013,
  kCGLBadAddress = 10014,
  kCGLBadCodeModule = 10015,
  kCGLBadAlloc = 10016,
  kCGLBadConnection = 10017
} CGLError;

#ifdef __cplusplus
}
#endif
#endif
