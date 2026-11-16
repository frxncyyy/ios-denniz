#ifndef TEGU_MEMORY_H
#define TEGU_MEMORY_H
#ifdef KERNEL
/* XNU supplies fortified string macros; preserve their native declarations. */
#include <string.h>
#else
#include <stddef.h>
/* Shared builders also compile as freestanding ARM64 without an SDK. Use
 * standard C ABI declarations instead of importing the build host's libc
 * headers into that target. The kernel/libSystem supplies these functions. */
#ifdef __cplusplus
extern "C" {
#endif
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
int memcmp(const void *,const void *,size_t);
size_t strlen(const char *);
#ifdef __cplusplus
}
#endif
#endif
#endif
