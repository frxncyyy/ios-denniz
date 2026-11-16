#ifndef TEGU_NATIVE_PACKAGE_V7_WRITER_H
#define TEGU_NATIVE_PACKAGE_V7_WRITER_H
#include "native_package_v7.h"
/* Userland-only heap package builder; do not compile into the kernel client. */
int tegu_native_v7_build(const void *,size_t,const void *,size_t,uint8_t **,size_t *);
int tegu_native_v7_build_with_blend(const void *,size_t,const void *,size_t,uint32_t,uint8_t **,size_t *);
void tegu_native_v7_free(uint8_t **,size_t *);
#endif
