#!/usr/bin/env python3
"""Compile native GPU offsets/commands against actual Android JM headers."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('android',type=Path);a=p.parse_args()
base=(a.android/'drivers/gpu/mediatek/gpu_mali/mali_valhall/mali-r32p1/drivers/gpu/arm/midgard/include/uapi/gpu/arm/midgard/gpu').resolve()
out=ROOT/'gpu/build/android-register-oracle';out.mkdir(exist_ok=True)
header=base/'mali_kbase_gpu_regmap.h'
macros=subprocess.check_output(['cc','-DMALI_USE_CSF=0','-DIS_ENABLED(x)=0','-dM','-E','-include',str(header),'-'],input=b'').decode()
known=set(re.findall(r'^#define (\w+)',macros,re.M))
ours=(ROOT/'gpu/include/nord2_gpu_hw.h').read_text();pairs=[];unmatched=[]
for n in re.findall(r'^\s*(N2HW_[A-Z0-9_]+)\s*=',ours,re.M):
    v=n[5:]
    if v not in known:
        if not n.startswith('N2HW_E_') and n!='N2HW_OK':unmatched.append(n)
        continue
    if v.startswith('JOB_IRQ_'):v=f'JOB_CONTROL_REG({v})'
    elif v.startswith('MMU_IRQ_'):v=f'MMU_REG({v})'
    pairs.append((n,v))
# Same hardware masks/commands with differently named public helpers.
extra={
 'N2HW_CMD_SOFT_RESET':'GPU_COMMAND_SOFT_RESET',
 'N2HW_CMD_HARD_RESET':'GPU_COMMAND_HARD_RESET',
 'N2HW_CMD_CLEAN_CACHES':'GPU_COMMAND_CLEAN_CACHES',
 'N2HW_CMD_CLEAN_INV_CACHES':'GPU_COMMAND_CLEAN_INV_CACHES',
 'N2HW_COHERENCY_NONE':'COHERENCY_NONE',
 'N2HW_JS_CMD_START':'JS_COMMAND_START',
 'N2HW_JS_CMD_SOFT_STOP':'JS_COMMAND_SOFT_STOP',
 'N2HW_JS_CMD_HARD_STOP':'JS_COMMAND_HARD_STOP',
 'N2HW_AS_CMD_UPDATE':'AS_COMMAND_UPDATE',
 'N2HW_AS_CMD_LOCK':'AS_COMMAND_LOCK',
 'N2HW_AS_CMD_FLUSH_MEM':'AS_COMMAND_FLUSH_MEM',
 'N2HW_AS_CMD_FLUSH_PT':'AS_COMMAND_FLUSH_PT',
}
for k,v in extra.items():
    assert v in known,v
    pairs.append((k,v))
s='#include "nord2_gpu_hw.h"\n#define MALI_USE_CSF 0\n#define IS_ENABLED(x) 0\n#include "'+str(header)+'"\n'
s+='\n'.join(f'_Static_assert({k} == ({v}), "{k}");' for k,v in pairs)+'\nint main(void){return 0;}\n'
(out/'oracle.c').write_text(s)
subprocess.run(['cc','-std=c11','-Wall','-Werror','-I'+str(ROOT/'gpu/include'),str(out/'oracle.c'),'-o',str(out/'oracle')],check=True)
subprocess.run([str(out/'oracle')],check=True)
receipt={'checked':len(pairs),'pairs':pairs,'unmatched':unmatched,'headers':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [header,base/'backend/mali_kbase_gpu_regmap_jm.h',base/'mali_kbase_gpu_coherency.h',base/'mali_kbase_gpu_id.h']},'passed':True}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(f'Android JM register oracle: {len(pairs)} offsets/commands match; unmatched offsets: {unmatched}')
