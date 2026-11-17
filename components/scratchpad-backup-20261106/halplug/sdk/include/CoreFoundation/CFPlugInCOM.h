#pragma once
#include <CoreFoundation/CoreFoundation.h>
typedef SInt32 HRESULT;
typedef UInt32 ULONG;
typedef void *LPVOID;
typedef CFUUIDBytes REFIID;
#define S_OK ((HRESULT)0x00000000L)
#define E_NOINTERFACE ((HRESULT)0x80000004L)
#define IUnknownUUID CFUUIDGetConstantUUIDWithBytes(kCFAllocatorSystemDefault, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46)
