#ifndef TEGU_SINGLE_PASS_BLUR_NATIVE_PACKAGE_B3_WRITER_H
#define TEGU_SINGLE_PASS_BLUR_NATIVE_PACKAGE_B3_WRITER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int tegu_native_b3_build(const void *,size_t,const void *,size_t,uint8_t **,size_t *);
void tegu_native_b3_free(uint8_t **,size_t *);
#ifdef __cplusplus
}
#endif
#endif
