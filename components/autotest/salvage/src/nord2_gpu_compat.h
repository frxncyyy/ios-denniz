#ifndef NORD2_GPU_COMPAT_H
#define NORD2_GPU_COMPAT_H
#if defined(KERNEL) || defined(_KERNEL)
#include <libkern/libkern.h>
#else
#include <string.h>
#endif
#endif
