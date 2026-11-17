#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
source=(HERE/'sweet_ufs.c').read_text();base=(HERE/'sweet_ufs.base.c').read_text()
assert source.count('#include "native_adapter.inc"')==1
assert source.index('nord2_clock_init();')<source.index('lck_mtx_lock(&sweet_ufs_lock);',source.index('sweet_ufs_init(void)'))
assert 'lck_mtx_unlock(&sweet_ufs_lock);\n\tnord2_clock_sample("first-gui");' in source
assert 'nord2_clock' not in source[source.index('sweet_ufs_strategy(buf_t'):source.index('static int\nsweet_ufs_ioctl',source.index('sweet_ufs_strategy(buf_t'))] if 'static int\nsweet_ufs_ioctl' in source[source.index('sweet_ufs_strategy(buf_t'):] else True
with tempfile.TemporaryDirectory(prefix='nord2-clock-native-') as td:
 p=Path(td);c=p/'test.c';exe=p/'test'
 c.write_text(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
typedef uintptr_t vm_offset_t;
static uint32_t ap[0x4000/4],mcu[0x10000/4],arg;
static unsigned maps,mapfail,logs;
static bool nord2_ufs_profile_enabled;
static int PE_parse_boot_argn(const char *name,void *v,unsigned n){assert(!strcmp(name,"nord2cpuclock")&&n==4);*(uint32_t*)v=arg;return 1;}
static vm_offset_t ml_io_map(uint64_t physical,unsigned size){maps++;if(maps==mapfail)return 0;
 if(physical==0x1000c000){assert(size==0xe00);return (uintptr_t)ap;}
 assert(physical==0xc530000&&size==0x10000);return (uintptr_t)mcu;}
static uint64_t rsr(const char *s){assert(!strcmp(s,"CNTFRQ_EL0"));return 13000000;}
#define __builtin_arm_rsr64 rsr
static int bounded_printf(const char *f,...){char buf[512];va_list va;va_start(va,f);int n=vsnprintf(buf,sizeof(buf),f,va);va_end(va);assert(n>0&&n<255);logs++;return n;}
#define printf bounded_printf
#include "native_adapter.inc"
static void reset(void){nord2_clock_enabled=false;nord2_clock_apmixed=nord2_clock_mcucfg=0;maps=logs=mapfail=0;
 memset(ap,0,sizeof(ap));memset(mcu,0,sizeof(mcu));
 for(unsigned i=0;i<5;i++){ap[(n2clk_read_allowlist[i]-0x1000c000)/4]=0x140000;mcu[(n2clk_read_allowlist[5+i]-0xc530000)/4]=(8<<17)|(1<<9);}}
int main(void){
 reset();arg=0;nord2_ufs_profile_enabled=true;nord2_clock_init();nord2_clock_sample("first-gui");assert(!maps&&!logs);
 reset();arg=2;nord2_clock_init();assert(!maps&&!logs);
 reset();arg=1;nord2_ufs_profile_enabled=false;nord2_clock_init();assert(!maps&&!logs);
 reset();nord2_ufs_profile_enabled=true;nord2_clock_init();assert(maps==2&&logs==13);
 assert(nord2_clock_capture.status==N2CLK_OK&&nord2_clock_capture.attempted==30);
 nord2_clock_sample("first-gui");assert(maps==2&&logs==26);
 uint32_t v=0x12345678;assert(nord2_clock_read(0,0x1000c210,&v)==-2&&v==0x12345678);
 assert(nord2_clock_read(0,0x1000c20d,&v)==-2&&v==0x12345678);
 for(unsigned fail=1;fail<=2;fail++){
  reset();mapfail=fail;nord2_clock_init();assert(nord2_clock_capture.status==N2CLK_READ_ERROR);
  assert(logs==14&&maps==fail&&nord2_clock_capture.attempted==(fail==1?1:6));
 }
 reset();nord2_clock_enabled=true;nord2_clock_apmixed=(uintptr_t)ap;nord2_clock_mcucfg=(uintptr_t)mcu;
 for(unsigned i=0;i<5;i++){
  nord2_clock_capture.pairs[i].configured_khz=UINT64_MAX;
 }
 /* Direct report collector also handles stable unsupported mux without rate. */
 mcu[(0xa2a0)/4]=0;nord2_clock_sample("pre-root");assert(nord2_clock_capture.status==N2CLK_NON_ARMPLL&&!nord2_clock_capture.pairs[0].configured_khz);
 return 0;}
''')
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(HERE),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS exact nativeadapter: opt-in/profile gates, source-backed mappings, allowlist, two13-line snapshots outsideUFSmutex, mapfail/no-retry, unknownmux; ASan+UBSan')
