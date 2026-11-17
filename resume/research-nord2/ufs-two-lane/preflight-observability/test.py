#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
# Run existing full engine fault suite against PRIVATE candidate (no canonical edits).
s=(ROOT/'tools/test-nord2-ufs-two-lane.py').read_text()
s=s.replace("ROOT=Path(__file__).resolve().parents[1]",f"ROOT=Path({str(ROOT)!r})")
s=s.replace("HERE=ROOT/'shim/nord2-xnu'",f"HERE=Path({str(HERE)!r})")
exec(compile(s,'engine-suite','exec'),{'__name__':'__main__','__file__':str(HERE/'test.py')})
# Reuse exact adapter full integration harness with private paths; parity checks
# intentionally omitted because this is an isolated, unintegrated candidate.
s=(ROOT/'tools/test-nord2-ufs-two-lane-adapter.py').read_text()
s=s[s.index('with tempfile.TemporaryDirectory'):]
s=s.replace('assert(logs==7)','assert(logs==20)')
s=s.replace('su.dma_pa,su.dma};','su.dma_pa,su.dma,0,0,{0},1};')
s=s.replace('assert(!nord2_x2_owned(&c));','c.observing=1;c.valid=c.fail=0;assert(!nord2_x2_owned(&c));assert(c.fail && c.valid==((1u<<c.fail)-1));')
s=s.replace('assert(integrated.original_verified', 'assert(integrated.preflight_valid==0x3fff && !integrated.preflight_fail);assert(integrated.owned_valid==0x7fffff && !integrated.owned_fail);\n assert(integrated.original_verified')
exec(compile(s,'adapter-suite','exec'),{'Path':Path,'subprocess':subprocess,'tempfile':tempfile,'ROOT':ROOT,'P':HERE,'adapter':(HERE/'ufs_two_lane_adapter.inc').read_text()})
# Differential first-gate test: compare EXACT old short-circuit access trace to
# new implementation, including HCS double read, every failure and final IS.
old=(ROOT/'shim/nord2-xnu/ufs_two_lane.c').read_text()
new=(HERE/'ufs_two_lane.c').read_text()
def extract(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
old=extract(old,'static int healthy(','static int allowed(').replace('healthy(', 'original(')
new=extract(new,'static int gate(','static int allowed(')
with tempfile.TemporaryDirectory() as td:
 p=Path(td); c=p/'d.c';exe=p/'d'
 c.write_text('''#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "ufs_two_lane.h"
#define HCS 0x30u
struct run{const struct n2l_ops *o;void*c;struct n2l_result*r;uint64_t start;int observing;};
static unsigned trace[32],count,fail;
static uint32_t readcb(void*c,uint32_t a){(void)c;trace[count++]=a;
 unsigned i=count;uint32_t v=a==0x34?1:a==0x30?0x10f:0;
 if(i==fail){if(a==0x34)v=0;else if(a==0x30)v=i==2?0:0x30;else v=1;}return v;}
static int owned(void*c){(void)c;trace[count++]=0xffff;return fail!=1;}
static uint32_t rd(struct run*s,uint32_t a){return s->o->read32(s->c,a);}
'''+old+new+'''
int main(void){for(fail=0;fail<=14;fail++){
 struct n2l_ops o={.read32=readcb,.owned_preroot=owned};struct n2l_result r={0};struct run s={&o,0,&r,0,1};
 count=0;int before=original(&s);if(before)before=!rd(&s,0x20);unsigned n=count, saved[32];memcpy(saved,trace,sizeof(trace));
 count=0;int after=healthy(&s);if(after){uint32_t v=rd(&s,0x20);after=gate(&s,13,v,v==0);}
 assert(before==after && n==count && !memcmp(saved,trace,n*sizeof(unsigned)));
 assert(r.preflight_valid==((1u<<count)-1));assert(r.preflight_fail==fail);
 }return 0;}
''')
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(HERE),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS differential initial gate: unchanged short-circuit reads, first failure, explicit validity for all14 evaluations')
# Extra labels do not masquerade as any of the original seven report records.
import runpy
oldtest=runpy.run_path(str(HERE.parent/'hardware-candidate/test-parser.py'))
extra='NORD2 UFS X2 PREFLIGHT: valid=0x3fff fail=0 owned_valid=0x7fffff owned_fail=0\n'
extra+=''.join(f'NORD2 UFS X2 GATE {i}: v{i}=0\n' for i in (0,4,8,12))
extra+=''.join(f'NORD2 UFS X2 OWN {i}: v{i}=0\n' for i in range(0,23,3))
before=oldtest['parse'](oldtest['raw']);after=oldtest['parse'](oldtest['raw']+extra)
assert before['success']==after['success'] and before['complete']==after['complete']
assert before['records']==after['records']
print('PASS original seven-line parser ignores distinct observability labels')
