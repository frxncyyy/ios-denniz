#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
platform=ROOT.parent/'nord2-current-kernel/drivers/misc/mediatek/base/power/cpufreq_v1/src/mach/mt6893/mtk_cpufreq_platform.c'
s=platform.read_text();a=s.index('static unsigned int pll_to_clk(');b=s.index('unsigned int get_cur_phy_freq(',a)
# Exact selected vendor pure math is the independent expected-value oracle.
ref=s[a:b].replace('_GET_BITS_VAL_(26:24, con1)','((con1 >> 24) & 7)').replace('_BITMASK_(21:0)','0x3fffff')
with tempfile.TemporaryDirectory(prefix='nord2-configured-clock-') as td:
 p=Path(td);c=p/'test.c';exe=p/'test'
 c.write_text('''#include "configured_clock.h"
#include <assert.h>
#include <string.h>
'''+ref+'''
static unsigned calls,fail_at,change_at;static int poisoned;
static int readfn(void *ctx,uint64_t addr,uint32_t *v){(void)ctx;
 unsigned i=calls%10;assert(addr==n2clk_read_allowlist[i]);calls++;
 if(calls==fail_at){*v=0xdeadbeef;poisoned=1;return -17;}
 assert(!poisoned);*v=i<5?0x140000:(8u<<17)|(1u<<9);
 if(calls==change_at)*v^=1;return 0;}
int main(void){
 const uint64_t addr[10]={0x1000c20c,0x1000c22c,0x1000c23c,0x1000c24c,0x1000c21c,0xc53a2a0,0xc53a2a4,0xc53a2a8,0xc53a2ac,0xc53a2b0};
 assert(!memcmp(addr,n2clk_read_allowlist,sizeof(addr)));
 const unsigned pcws[]={1,16383,16384,0x140000,0x3fffff};
 const unsigned divs[]={8,9,10,11,16,17,18,19,20,24,25,26,27,28,29};
 struct n2clk_decoded out;
 for(unsigned p=0;p<4;p++)for(unsigned j=0;j<5;j++)for(unsigned k=0;k<15;k++){
  uint32_t con=pcws[j]|(p<<24)|0x80000000u,div=(divs[k]<<17)|(1<<9)|0x40000000u;
  assert(n2clk_decode(con,div,&out)==N2CLK_OK);
  assert(out.configured_khz==_cpu_freq_calc(con,divs[k]));
  assert(out.con1==con&&out.divider_word==div&&out.mux==1);
 }
 for(unsigned d=0;d<32;d++){
  unsigned known=0;for(unsigned k=0;k<15;k++)if(d==divs[k])known=1;
  assert(n2clk_decode(0x140000,(d<<17)|(1<<9),&out)==(known?N2CLK_OK:N2CLK_UNKNOWN_DIVIDER));
  if(!known)assert(!out.configured_khz);
 }
 for(unsigned m=0;m<4;m++)if(m!=1){assert(n2clk_decode(0x140000,(8<<17)|(m<<9),&out)==N2CLK_NON_ARMPLL);assert(!out.configured_khz);}
 for(unsigned p=4;p<8;p++){assert(n2clk_decode(0x140000|(p<<24),(8<<17)|(1<<9),&out)==N2CLK_UNKNOWN_POSTDIV);assert(!out.configured_khz);}
 assert(n2clk_decode(0,(8<<17)|(1<<9),&out)==N2CLK_ZERO_PCW);
 assert(n2clk_decode(0,0,0)==N2CLK_BAD_ARGUMENT);
 struct n2clk_capture cap;
 assert(n2clk_collect(readfn,0,&cap)==N2CLK_OK&&calls==30&&cap.attempted==30);
 for(unsigned p=0;p<3;p++)assert(cap.valid[p]==0x3ff);
 for(unsigned i=0;i<5;i++)assert(cap.pairs[i].status==N2CLK_OK&&cap.pairs[i].configured_khz==2080000);
 for(fail_at=1;fail_at<=30;fail_at++){
  calls=poisoned=0;memset(&cap,0xff,sizeof(cap));
  assert(n2clk_collect(readfn,0,&cap)==N2CLK_READ_ERROR);
  assert(calls==fail_at&&cap.attempted==fail_at);
  for(unsigned p=0;p<3;p++)for(unsigned i=0;i<10;i++){
   unsigned observed=p*10+i+1<fail_at;
   assert(!!(cap.valid[p]&(1u<<i))==observed);
   if(!observed)assert(cap.words[p][i]==0);
  }
  for(unsigned i=0;i<5;i++)assert(cap.pairs[i].status==N2CLK_BAD_ARGUMENT&&!cap.pairs[i].configured_khz);
 }
 fail_at=poisoned=0;
 for(change_at=1;change_at<=30;change_at++){
  calls=0;assert(n2clk_collect(readfn,0,&cap)==N2CLK_UNSTABLE&&calls==30);
  for(unsigned i=0;i<5;i++)assert(!cap.pairs[i].configured_khz);
 }
 assert(n2clk_collect(0,0,&cap)==N2CLK_BAD_ARGUMENT&&cap.attempted==0);
 assert(n2clk_collect(readfn,0,0)==N2CLK_BAD_ARGUMENT);
 return 0;}
''')
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(HERE),str(c),str(HERE/'configured_clock.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 subprocess.run(['clang','--target=aarch64-none-elf','-std=c11','-ffreestanding','-Wall','-Wextra','-Werror','-I'+str(HERE),'-c',str(HERE/'configured_clock.c'),'-o',str(p/'clock.o')],check=True)
print('PASS: selected vendor math oracle300cases, unknownmux/div/postdiv, fixed30readallowlist, all30readfailures/unstablesamples, rawvalidity/nohiddenretry; ASan+UBSan+AArch64')
