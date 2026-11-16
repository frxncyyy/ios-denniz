#!/usr/bin/env python3
"""Compare production workaround writes to unchanged Android r32p1 helpers.

Extracted vendor source and executable stay in ignored build/. This proves
source/protocol agreement under a register model, not hardware completion.
"""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('android',type=Path);a=p.parse_args()
source=a.android/'drivers/gpu/mediatek/gpu_mali/mali_valhall/mali-r32p1/drivers/gpu/arm/midgard/mali_kbase_dummy_job_wa.c'
s=source.read_text(); funcs=[]
for name in ['wait_any','wait','run_job','kbase_dummy_job_wa_execute']:
    m=re.search(r'^(?:static (?:inline )?)?(?:u32|int) '+name+r'\([^;]*?\)\n\{',s,re.M)
    assert m,name
    pos=m.end();level=1
    while level:
        if s[pos]=='{':level+=1
        if s[pos]=='}':level-=1
        pos+=1
    funcs.append(s[m.start():pos])
out=ROOT/'gpu/build/android-wa-oracle';out.mkdir(exist_ok=True)
pre='''#define main old_hardware_tests_main
#include "nord2_hw_test.c"
#undef main
#include <errno.h>
#include <sys/types.h>
typedef uint32_t u32;
typedef uint64_t u64;
#define U32_MAX UINT32_MAX
#define dev_err(...) ((void)0)
#define KBASE_DUMMY_JOB_WA_FLAG_SERIALIZE 1
#define KBASE_DUMMY_JOB_WA_FLAG_WAIT_POWERUP 2
#define KBASE_DUMMY_JOB_WA_FLAG_LOGICAL_SHADER_POWER 4
#define GPU_CONTROL_REG(x) (x)
#define JOB_CONTROL_REG(x) (x)
#define JOB_SLOT_REG(s,x) N2HW_JS_REG(s,x)
#define JS_CONFIG_DISABLE_DESCRIPTOR_WR_BK (1u<<15)
#define JS_COMMAND_START 1
struct kbase_context { int as_nr; };
struct kbase_device {
 void *dev;
 struct {struct kbase_context *ctx;int slot;u64 jc,flags;} dummy_job_wa;
};
static struct model *active;
static uint32_t kbase_reg_read(struct kbase_device*k,uint32_t off){(void)k;return rd(active,off);}
static void model_write(struct model*m,uint32_t off,uint32_t val){
 wr(m,off,val);
 if(off==N2HW_JS_REG(0,N2HW_JS_COMMAND_NEXT) && val==1){
   *R(m,N2HW_JOB_IRQ_RAWSTAT)=1;
   *R(m,off)=0;
 }
}
static void kbase_reg_write(struct kbase_device*k,uint32_t off,uint32_t val){(void)k;model_write(active,off,val);}
static void udelay(unsigned us){dly(active,us);}
'''
names=['GPU_IRQ_MASK','JOB_IRQ_MASK','SHADER_PWRON_LO','SHADER_PWRON_HI','SHADER_READY_LO','SHADER_READY_HI','SHADER_PWROFF_LO','SHADER_PWROFF_HI','SHADER_PWRTRANS_LO','SHADER_PWRTRANS_HI','GPU_IRQ_CLEAR','JS_HEAD_NEXT_LO','JS_HEAD_NEXT_HI','JS_AFFINITY_NEXT_LO','JS_AFFINITY_NEXT_HI','JS_CONFIG_NEXT','JS_COMMAND_NEXT','JOB_IRQ_RAWSTAT','JOB_IRQ_CLEAR','JS_STATUS']
pre+=''.join(f'#define {n} N2HW_{n}\n' for n in names)
post='''
static void ourwrite(void*v,uint32_t o,uint32_t n){model_write(v,o,n);}
int main(void){
 struct model vendor, ours; struct n2hw hw;
 model_init(&vendor,0x90800011);model_init(&ours,0x90800011);
 *R(&vendor,N2HW_GPU_IRQ_MASK)=*R(&ours,N2HW_GPU_IRQ_MASK)=0x123;
 *R(&vendor,N2HW_JOB_IRQ_MASK)=*R(&ours,N2HW_JOB_IRQ_MASK)=0x321;
 struct kbase_context ctx={.as_nr=0};
 struct kbase_device kb={.dummy_job_wa={.ctx=&ctx,.slot=0,.jc=0x100000000ull,.flags=6}};
 active=&vendor;CHECK(kbase_dummy_job_wa_execute(&kb,N2HW_MT6893_SHADER_PRESENT_9)==0);
 const struct n2hw_ops ho={rd,ourwrite,dly,NULL};
 CHECK(n2hw_init(&hw,&ho,&ours));CHECK(n2hw_identify(&hw)==N2HW_OK);
 CHECK(n2hw_select_slots(&hw)==N2HW_OK);hw.l2_on=true;
 CHECK(n2hw_dummy_job_wa(&hw)==N2HW_OK);
 unsigned j=0;
 for(unsigned i=0;i<ours.wlog;i++){
  /* Additional stale IRQ clear before START is deliberate, not vendor sequence. */
  if(ours.wlog_off[i]==N2HW_JOB_IRQ_CLEAR && ours.wlog_val[i]==0x10001)continue;
  CHECK(j<vendor.wlog);
  if(j<vendor.wlog){CHECK(ours.wlog_off[i]==vendor.wlog_off[j]);CHECK(ours.wlog_val[i]==vendor.wlog_val[j]);}j++;
 }
 CHECK(j==vendor.wlog);
 printf("Android unchanged WA execute/run_job/wait helpers: %u ordered register writes matched; failures=%u\\n",j,failures);
 return failures?1:0;
}
'''
(out/'oracle.c').write_text(pre+'\n\n'.join(funcs)+post)
cmd=['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'gpu/include'),'-I'+str(ROOT/'gpu/tests'),str(out/'oracle.c'),str(ROOT/'gpu/src/nord2_gpu_hw.c'),'-o',str(out/'oracle')]
subprocess.run(cmd,check=True);subprocess.run([str(out/'oracle')],check=True)
(out/'receipt.json').write_text(json.dumps({'source':str(source.resolve()),'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'functions_sha256':[hashlib.sha256(f.encode()).hexdigest() for f in funcs],'compiled':True,'passed':True,'device_tested':False},indent=2)+'\n')
