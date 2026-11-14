/* Minimal stand-in for the kernel's linux/compiler.h, needed only so that the UAPI videodev2.h
 * loads for probe_kernel.c outside a kernel build.  Nothing here affects a struct layout.
 * Same technique as ap/test_v41/kstub (V41); this copy belongs to DEC01. */
#ifndef DEC01_KSTUB_COMPILER_H
#define DEC01_KSTUB_COMPILER_H
#define __user
#define __force
#define __iomem
#define __bitwise__
#ifndef __always_inline
#define __always_inline inline
#endif
struct timeval { long tv_sec; long tv_usec; };
struct timespec { long tv_sec; long tv_nsec; };
#endif
