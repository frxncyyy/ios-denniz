#include <assert.h>
#include <stdio.h>
#include "surface_display_policy.h"

static int candidate(const char *name, int property_present,
    uint32_t width, uint32_t height, uint32_t format, uint16_t bpe,
    uint32_t row, uint32_t planes, uint32_t offset, uint32_t size,
    uint8_t ew, uint8_t eh)
{
    return tegu_surface_auto_gpu_display(name, property_present, width, height,
        format, bpe, row, planes, offset, size, ew, eh);
}

int main(void)
{
    const char *name = "backboardd";
#define EXACT(n,p,w,h,f,b,r,pc,o,s,ew,eh) candidate(n,p,w,h,f,b,r,pc,o,s,ew,eh)
    assert(EXACT(name,0,1125,2436,0x42475241u,4,4608,0,0,11239424,1,1));
    assert(!EXACT("SpringBoard",0,1125,2436,0x42475241u,4,4608,0,0,11239424,1,1));
    assert(!EXACT("backboarddx",0,1125,2436,0x42475241u,4,4608,0,0,11239424,1,1));
    assert(!EXACT(name,1,1125,2436,0x42475241u,4,4608,0,0,11239424,1,1));
    assert(!EXACT(name,0,1124,2436,0x42475241u,4,4608,0,0,11239424,1,1));
    assert(!EXACT(name,0,1125,2435,0x42475241u,4,4608,0,0,11239424,1,1));
    assert(!EXACT(name,0,1125,2436,0x62336138u,4,4608,0,0,11239424,1,1));
    assert(!EXACT(name,0,1125,2436,0x42475241u,8,4608,0,0,11239424,1,1));
    assert(!EXACT(name,0,1125,2436,0x42475241u,4,4544,0,0,11239424,1,1));
    assert(!EXACT(name,0,1125,2436,0x42475241u,4,4608,1,0,11239424,1,1));
    assert(!EXACT(name,0,1125,2436,0x42475241u,4,4608,0,4096,11239424,1,1));
    assert(!EXACT(name,0,1125,2436,0x42475241u,4,4608,0,0,11239423,1,1));
    assert(!EXACT(name,0,1125,2436,0x42475241u,4,4608,0,0,11239424,2,1));
    assert(!EXACT(name,0,1125,2436,0x42475241u,4,4608,0,0,11239424,1,2));
#undef EXACT
    puts("PASS exact BackBoard linear display GPU-backing policy");
    return 0;
}
