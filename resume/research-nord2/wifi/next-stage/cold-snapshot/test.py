#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "snapshot.h"
#include "snapshot.c"
typedef uintptr_t vm_offset_t;
static struct {vm_offset_t spm,infra,gals;bool saved,domain_touched;} nw;
static unsigned enabled,reads,newreads,mapcalls,logs,restores,guard_fail,read_fail;
static uint32_t raw[3];
static int PE_parse_boot_argn(const char *key,unsigned *value,unsigned size){assert(!strcmp(key,"nord2wificold") && size==4);*value=enabled;return 1;}
static vm_offset_t ml_io_map(uint64_t address,unsigned bytes){assert(address==0x18000000 && bytes==4096);mapcalls++;return (vm_offset_t)address;}
static uint32_t ni_rd(vm_offset_t base,unsigned offset){
 uint64_t address=base+offset;reads++;
 switch(address){
 case 0x10006304:return guard_fail==1?0:0xd;
 case 0x1000616c:return guard_fail==2?0:2;
 case 0x10006170:return guard_fail==3?0:2;
 case 0x1020e504:return guard_fail==4?0:1;
 case 0x10001228:return guard_fail==5?0x6000:0;
 case 0x10001724:return guard_fail==6?4:0;
 case 0x18060184:return guard_fail==7?0x20:0;
 case 0x18000010:newreads++;return raw[0];
 case 0x18000000:newreads++;return raw[1];
 case 0x180602cc:newreads++;return raw[2];
 default:assert(!"nonallowlisted MMIO");return 0;
 }
}
static int log_line(const char *format,...){
 char line[256];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof(line),format,args);va_end(args);assert(n>=0&&n<255);logs++;return 0;
}
#define printf log_line
#include "adapter.inc"
static void reset(void){nw.spm=0x10006000;nw.infra=0x10001000;nw.gals=0x1020e000;nw.saved=nw.domain_touched=true;enabled=1;reads=newreads=mapcalls=logs=restores=guard_fail=read_fail=0;memset(raw,0,sizeof(raw));}
static int count_check(void *ctx,unsigned mask){assert(!ctx&&mask==NWC_REQUIRED);reads++;return guard_fail==reads?-1:0;}
static int count_read(void *ctx,uint64_t address,uint32_t *value){assert(!ctx);const uint64_t addresses[3]={0x18000010,0x18000000,0x180602cc};assert(newreads<3&&address==addresses[newreads]);newreads++;if(read_fail==newreads)return -1;*value=raw[newreads-1];return 0;}
static void guarded_window(void){nwc_power_window_snapshot(0x18060000,0x20010101);restores++;}
int main(void){
 const struct nwc_ops ops={count_check,count_read};struct nwc_result r;
 reset();assert(!nwc_snapshot(&ops,NULL,1,&r));assert(r.cold && r.valid_mask==7 && r.attempted==3);
 for(unsigned opt=0;opt<4;opt++)if(opt!=1){reset();assert(nwc_snapshot(&ops,NULL,opt,&r)==-1 && !reads && !newreads);}
 for(unsigned i=1;i<=3;i++){
  reset();guard_fail=i;assert(nwc_snapshot(&ops,NULL,1,&r)==-1 && r.error==-2 && newreads==i-1 && r.valid_mask==((1u<<(i-1))-1));
  reset();read_fail=i;assert(nwc_snapshot(&ops,NULL,1,&r)==-1 && r.error==-3 && newreads==i && r.valid_mask==((1u<<(i-1))-1));
 }
 const unsigned mask[3]={1,0x80,4};
 for(unsigned i=0;i<3;i++){reset();raw[i]=mask[i];assert(nwc_snapshot(&ops,NULL,1,&r)==1 && !r.cold && r.valid_mask==7);}
 reset();raw[0]=0xfffffffe;raw[1]=0xffffff7f;raw[2]=0xfffffffb;assert(!nwc_snapshot(&ops,NULL,1,&r)&&r.cold);
 // Exact native-adapter candidate: only the powered/acknowledged window.
 reset();guarded_window();assert(mapcalls==1 && newreads==3 && logs==1 && restores==1);
 for(unsigned i=1;i<=7;i++){reset();guard_fail=i;guarded_window();assert(!newreads && logs==1 && restores==1);}
 reset();enabled=0;guarded_window();assert(!mapcalls&&!newreads&&!logs&&restores==1);
 reset();enabled=2;guarded_window();assert(!mapcalls&&!newreads&&!logs&&restores==1);
 reset();nw.saved=false;guarded_window();assert(!mapcalls&&!newreads&&restores==1);
 reset();nw.domain_touched=false;guarded_window();assert(!mapcalls&&!newreads&&restores==1);
 reset();nwc_power_window_snapshot(0x18060000,0xdeadbeef);assert(!mapcalls&&!newreads);
 reset();struct nwc_adapter c={0x18000000,0x18060000,0x20010101};uint32_t value;
 assert(nwc_adapter_read(&c,0x184b0010,&value)==-1 && !newreads);
 assert(nwc_adapter_read(&c,0x18000018,&value)==-1 && !newreads);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(HERE),str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
print('PASS exact3-register helper+private adapter: ASan/UBSan, every prereq/read failure, precise address/mask gates, cold/noncold validity, optout, always-restoration caller contract,255char log bound; no hardware')
