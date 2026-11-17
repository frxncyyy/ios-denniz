#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "audio_hw.h"
static struct nau_hw hardware;
const uint32_t nau_snapshot_pa[NAU_SNAP_COUNT]={0};
static char captured[2048];static unsigned used,status_calls;
void nau_hw_status(const struct nau_hw *h,struct nah_status *out){status_calls++;*out=h->status;}
static int capture(const char *format,...){va_list ap;va_start(ap,format);int n=vsnprintf(captured+used,sizeof(captured)-used,format,ap);va_end(ap);assert(n>=0&&(unsigned)n<sizeof(captured)-used);used+=(unsigned)n;return n;}
#define printf capture
#include "diagnose-under-test.inc"
#undef printf
int main(void){
 const uint32_t values[]={2,0x3c,2,1,0x8000};
 hardware.bus.ap_valid=NA8_AP_VALID_ALL;
 for(unsigned i=0;i<NA8_AP_FIELDS;i++)hardware.bus.ap_preflight[i]=values[i];
 hardware.status.i2c_stage=8;hardware.status.i2c_failure=7;
 hardware.status.i2c_last_pa=0x11d04110;hardware.status.i2c_last_value=0x3c;
 diagnose("init",-3,0);
 assert(status_calls==1);
 assert(strstr(captured,"i2cpa=11d04110 i2cv=3c"));
 assert(strstr(captured,"NORD2 AUDIO AP: valid=1f start=2 ctrl=3c irq=2 fifo=1 i3c=8000\n"));
 puts("PASS actualdiagnose: savedvalidmask/all5APvalues and refusedPA/value printed; noMMIOcallbacks linked/invoked");
}
