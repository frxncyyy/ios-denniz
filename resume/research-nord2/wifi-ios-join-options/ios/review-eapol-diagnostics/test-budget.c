#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
static unsigned lines,maxlen;
static int diag_printf(const char*f,...){char b[256];va_list a;va_start(a,f);int n=vsnprintf(b,sizeof b,f,a);va_end(a);assert(n>0&&n<255);if((unsigned)n>maxlen)maxlen=n;lines++;return n;}
#define printf diag_printf
#include "wifi_station_diag.c"
#undef printf
int main(void){struct nstd_record r={.generation=UINT64_MAX,.ticket=UINT64_MAX,.kind=UINT32_MAX};for(unsigned k=0;k<8;k++)r.v[k]=UINT32_MAX;for(unsigned k=0;k<1000;k++)nstd_log(&r);assert(lines==96);printf("PASS diagnostic global96 saturation, all-width maximum=%u bytes; suppression has no IO or status return\n",maxlen);return 0;}
