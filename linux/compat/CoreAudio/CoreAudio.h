// CoreAudio's names for the project's shared audio policy on Linux (shims/coreaudio/rs_audio_input.c, which the
// Linux build compiles as it is for its settings file, its list of entries and its level rule). The types and the
// constants it names; the calls it makes of a device are the layer's to answer (linux/darwin/dw_audio.c), and the
// functions of the file that make them are not the ones Linux uses.
#ifndef RS_COMPAT_COREAUDIO_H
#define RS_COMPAT_COREAUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t UInt32;
typedef int32_t OSStatus;
typedef uint32_t AudioObjectID;
typedef uint32_t AudioObjectPropertySelector;
typedef uint32_t AudioObjectPropertyScope;
typedef const void *CFStringRef;

typedef struct {
  UInt32 mNumberChannels, mDataByteSize;
  void *mData;
} AudioBuffer;
typedef struct {
  UInt32 mNumberBuffers;
  AudioBuffer mBuffers[1];
} AudioBufferList;

#define RS_FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
enum {
  kAudioObjectPropertyScopeInput = RS_FOURCC('i', 'n', 'p', 't'),
  kAudioDevicePropertyModelUID = RS_FOURCC('m', 'u', 'i', 'd'),
  kAudioDevicePropertyTransportType = RS_FOURCC('t', 'r', 'a', 'n'),
  kAudioDeviceTransportTypeBuiltIn = RS_FOURCC('b', 'l', 't', 'n'),
  kAudioDeviceTransportTypeAggregate = RS_FOURCC('g', 'r', 'u', 'p'),
  kAudioDeviceTransportTypeVirtual = RS_FOURCC('v', 'i', 'r', 't'),
  kCFStringEncodingUTF8 = 0x08000100,
};

// (the layer's own: dw_cf.c)
unsigned char dw_CFStringGetCString(const void *string, char *buffer, long size, uint32_t encoding);
void dw_CFRelease(void *object);
#define CFStringGetCString(s, b, n, e) dw_CFStringGetCString((s), (b), (long)(n), (e))
#define CFRelease(o) dw_CFRelease((void *)(uintptr_t)(o))

#endif
