/* Minimal iOS 16 CoreFoundation declarations for the Nord2 HAL plug-in (host build without the iPhoneOS SDK).
 * Layouts/values as in Apple's public CoreFoundation headers (CFBase.h, CFString.h, CFUUID.h). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef unsigned char Boolean;
typedef uint8_t UInt8; typedef int8_t SInt8; typedef uint16_t UInt16; typedef int16_t SInt16;
typedef uint32_t UInt32; typedef int32_t SInt32; typedef uint64_t UInt64; typedef int64_t SInt64;
typedef float Float32; typedef double Float64;
typedef SInt32 OSStatus;
typedef long CFIndex;
typedef const void *CFTypeRef;
typedef const struct __CFString *CFStringRef;
typedef const struct __CFAllocator *CFAllocatorRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef const struct __CFUUID *CFUUIDRef;
typedef CFTypeRef CFPropertyListRef;
typedef UInt32 CFStringEncoding;
#define kCFStringEncodingUTF8 0x08000100
typedef struct { UInt8 byte0, byte1, byte2, byte3, byte4, byte5, byte6, byte7, byte8, byte9, byte10, byte11, byte12, byte13,
    byte14, byte15; } CFUUIDBytes;
extern const CFAllocatorRef kCFAllocatorSystemDefault;
#define CFSTR(s) ((CFStringRef)__builtin___CFStringMakeConstantString("" s ""))
Boolean CFEqual(CFTypeRef, CFTypeRef);
void CFRelease(CFTypeRef);
CFTypeRef CFRetain(CFTypeRef);
const char *CFStringGetCStringPtr(CFStringRef, CFStringEncoding);
CFUUIDRef CFUUIDCreateFromUUIDBytes(CFAllocatorRef, CFUUIDBytes);
CFUUIDRef CFUUIDGetConstantUUIDWithBytes(CFAllocatorRef, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8,
    UInt8, UInt8, UInt8, UInt8, UInt8, UInt8);
