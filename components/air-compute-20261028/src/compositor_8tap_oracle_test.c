#include "compositor_8tap_oracle.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const uint8_t src[4]={64,128,192,255};
    const uint8_t clear[4]={204,102,51,255};
    uint8_t pixels[TEGU_8TAP_TARGET_PIXELS*4];
    assert(tegu_8tap_expected_constant_opaque(src,clear,pixels)==0);
    for(unsigned i=0;i<TEGU_8TAP_TARGET_PIXELS;i++) {
        assert(pixels[4*i+0]==192 && pixels[4*i+1]==128 &&
               pixels[4*i+2]==64 && pixels[4*i+3]==255);
    }
    const uint8_t src2[4]={0,255,17,255};
    assert(tegu_8tap_expected_constant_opaque(src2,clear,pixels)==0);
    for(unsigned i=0;i<TEGU_8TAP_TARGET_PIXELS;i++)
        assert(pixels[4*i+0]==17 && pixels[4*i+1]==255 &&
               pixels[4*i+2]==0 && pixels[4*i+3]==255);
    const uint8_t translucent[4]={1,2,3,254};
    assert(tegu_8tap_expected_constant_opaque(translucent,clear,pixels)!=0);
    assert(tegu_8tap_expected_constant_opaque(src,clear,NULL)!=0);
    puts("8-tap constant/opaque independent CPU pixel oracle PASS");
}
