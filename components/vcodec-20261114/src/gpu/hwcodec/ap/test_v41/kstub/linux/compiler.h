/* Minimal stand-in for the kernel's linux/compiler.h, needed only so that the UAPI videodev2.h
 * loads for probe_kernel.c outside a kernel build.  Nothing here affects a struct layout. */
#ifndef V41_KSTUB_COMPILER_H
#define V41_KSTUB_COMPILER_H
#define __user
#define __force
#define __iomem
#define __bitwise__
struct timeval { long tv_sec; long tv_usec; };
struct timespec { long tv_sec; long tv_nsec; };
#endif
