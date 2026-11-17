#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,difflib
p=Path(__file__).resolve().parent;root=p.parents[4]
original=(root/'shim/nord2-xnu/wifi_common_adapter.inc').read_text();candidate=(p/'wifi_common_adapter.candidate.inc').read_text()
assert candidate==original.replace('NCC_AP(4,nw.infra,0x228,0x6000,0)','NCC_AP(4,nw.infra,0x228,0x2000,0)')
# Compile actualfull owner+AP guard taken fromcandidate, no rewritten predicate.
a=candidate.index('static struct { unsigned captured,owner_ok');b=candidate.index('static void ncc_ap_report',a)
guard=candidate[a:b]
code='''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
static bool ncc_fresh_owned=true,ncc_latched_dirty=false;
static struct {bool saved,domain_touched;uint64_t spm,infra,gals;} nw={true,true,0x10006000,0x10001000,0x1020e000};
static struct {uint32_t id;uint64_t host;} ncc_native={0x20010101,0x18060000};
static uint32_t raw[7];static unsigned reads;
static uint32_t ni_rd(uint64_t base,unsigned offset){uint64_t a=base+offset;const uint64_t addresses[]={0x10006304,0x1000616c,0x10006170,0x1020e504,0x10001228,0x10001724,0x18060184};for(unsigned i=0;i<7;i++)if(a==addresses[i]){reads++;return raw[i];}assert(0);return 0;}
'''+guard+'''
static void reset(void){ncc_ap_failure=(__typeof__(ncc_ap_failure)){0};ncc_fresh_owned=true;ncc_latched_dirty=false;nw.saved=nw.domain_touched=true;reads=0;raw[0]=0xd;raw[1]=raw[2]=0x10580e03;raw[3]=1;raw[4]=0xe70c500e;raw[5]=0;raw[6]=0;}
int main(void){
 reset();assert(!ncc_ap_check()&&reads==7&&!ncc_ap_failure.captured); /*actualprotbit14high passes*/
 reset();raw[4]|=0x2000;assert(ncc_ap_check()==-1&&ncc_ap_failure.failed==4&&ncc_ap_failure.valid_mask==0x1f&&reads==5);
 reset();raw[4]=0x6000;assert(ncc_ap_check()==-1&&ncc_ap_failure.failed==4);
 reset();raw[5]=4;assert(ncc_ap_check()==-1&&ncc_ap_failure.failed==5&&ncc_ap_failure.valid_mask==0x3f&&reads==6);
 reset();raw[6]=0x20;assert(ncc_ap_check()==-1&&ncc_ap_failure.failed==6);
 reset();ncc_fresh_owned=false;assert(ncc_ap_check()==-1&&!reads&&ncc_ap_failure.owner_fail==1);
 reset();ncc_latched_dirty=true;assert(ncc_ap_check()==-1&&!reads&&ncc_ap_failure.owner_fail==2);
 for(unsigned i=0;i<4;i++){reset();raw[i]=0;assert(ncc_ap_check()==-1&&ncc_ap_failure.failed==i);}
 return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 q=Path(d);(q/'test.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(q/'test.c'),'-o',str(q/'test')],check=True);subprocess.run([str(q/'test')],check=True)
assert '(nw.pwr & 0x1f) != 0x12' in (p/'sweet_touch.candidate.c').read_text()
assert 'nw_wait(nw.infra, 0x228, 0x6000, 0)' in (p/'sweet_touch.candidate.c').read_text()
print('PASS exact1maskdiff, actuale70c500e passes persistentguard, bit13/6000/prot2/host/owner/powerreject, unchanged firstfailure/raw evidence and outer6000wait;ASan/UBSan')
