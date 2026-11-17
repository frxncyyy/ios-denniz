#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "audio_hw.h"
typedef uintptr_t vm_offset_t;
typedef uintptr_t thread_t;
#define PAGE_SIZE 0x4000u
static thread_t current_thread(void){return 1;}
static void delay(unsigned x){(void)x;}
#define NAH_NATIVE_BARRIER() ((void)0)
int naf_write_allowed(uint32_t p){(void)p;return 0;}
#include "audio_hw_native.inc"
int nah_board_register_allowed(unsigned r){(void)r;return 0;}
int nah_board_change_allowed(unsigned r,uint16_t o,uint16_t n){(void)r;(void)o;(void)n;return 0;}
#include "audio_i2c8.c"
#include "audio_amp.c"
static _Alignas(16384) uint8_t mem[9][16384];
static int lease(unsigned x){(void)x;return 0;}
int main(void){
 struct nah_native_context c={0};c.worker=1;c.i2c8_lease=lease;
 for(unsigned i=0;i<9;i++)c.windows[i]=(struct nah_native_window){nah_native_bases[i],(uintptr_t)mem[i]};
 assert(nah_native_valid(&c));
 const unsigned off[18]={0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,64,68,72};
 for(unsigned i=0;i<18;i++) {
  unsigned w=off[i]?2:1;assert(nah_native_write(&c,0x11d04100+off[i],w,off[i]+1)==0);
  uint32_t x=0;assert(nah_native_read(&c,0x11d04100+off[i],w,&x)==0&&x==off[i]+1);
 }
 assert(nah_native_write(&c,0x11d0414c,2,0x1234)==0); /* timeoutsetup+restore */
 assert(nah_native_write(&c,0x11d041ec,2,0x8000)==-1); /* DEBUG readonly */
 assert(nah_native_write(&c,0x11d041f0,2,0x1234)==-1); /* HFIFO never normal100k */
 assert(nah_native_write(&c,0x11d04024,2,2)==-1); /* shadowRESUME not allowed */
 assert(nah_native_write(&c,0x11d04224,2,1)==-1); /* CCU not owned */
 assert(nah_native_write(&c,0x11000788,4,1)==-1); /* no APDMA mutation */
 *(uint16_t*)(mem[6]+0x1ec)=0x8800;uint32_t debug=0;
 assert(nah_native_read(&c,0x11d041ec,2,&debug)==0&&debug==0x8800);
 assert(nah_native_read(&c,0x11000788,4,&debug)==0); /* APDMAidleguard */
 const struct setting *tables[3]={ra74,rb74,rc74};
 const unsigned counts[3]={sizeof ra74/sizeof*ra74,sizeof rb74/sizeof*rb74,sizeof rc74/sizeof*rc74};
 for(unsigned t=0;t<3;t++)for(unsigned i=0;i<counts[t];i++)assert(na8_silicon_value(tables[t][i].reg,tables[t][i].value));
 for(unsigned t=0;t<3;t++)assert(na8_classify((uint16_t)(0x0a74+256*t))==NA8_SUPPORTED);
 assert(na8_allowed(0x34,3)&&na8_allowed(0x37,0xfb)&&na8_allowed(0x34,0x14));
 assert(!na8_allowed(0x35,3)&&!na8_allowed(0x34,0xf0));
 puts("PASS actualnativeAPread/writewidths+15fieldrestorepaths;8800readable;HFIFO/shadow/CCU/APDMAwritesreject;all3TFA9874inittablesallowlisted");
}
