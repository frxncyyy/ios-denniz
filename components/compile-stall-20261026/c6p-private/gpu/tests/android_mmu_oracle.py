#!/usr/bin/env python3
"""Execute unchanged r32p1 flag encoding against actual GPU page-table leaves.

Vendor fragments/executable stay in ignored build/. This checks Android parity,
permissions, rejected remaps and CPU-page subdivision; it is not a GPU test.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('android', type=Path)
a = p.parse_args()
v = a.android / 'drivers/gpu/mediatek/gpu_mali/mali_valhall/mali-r32p1/drivers/gpu/arm/midgard'
paths = [v/'mmu/mali_kbase_mmu_mode_aarch64.c', v/'mali_kbase_mem.h', v/'mali_kbase_defs.h', v/'mali_kbase_mem.c', v/'include/uapi/gpu/arm/midgard/gpu/backend/mali_kbase_gpu_regmap_jm.h']
src, mem, defs, regions, regmap = [x.read_text() for x in paths]
assert re.search(r'^#define AS_MEMATTR_INDEX_DEFAULT\s+0\s*$', regmap, re.M)
match = re.search(r'^static u64 get_mmu_flags\([^;]*?\)\n\{', src, re.M)
assert match
end = match.end()
depth = 1
while depth:
    depth += (src[end] == '{') - (src[end] == '}')
    end += 1
function = src[match.start():end]
constants = []
for name in ['ENTRY_ACCESS_RW', 'ENTRY_ACCESS_RO', 'ENTRY_NX_BIT', 'ENTRY_ACCESS_BIT', 'ENTRY_IS_ATE_L3']:
    constants.append(re.search(r'^#define '+name+r'\s+[^\n]+', src, re.M).group())
for name in ['KBASE_REG_GPU_WR', 'KBASE_REG_GPU_RD', 'KBASE_REG_GPU_NX', 'KBASE_REG_SHARE_IN', 'KBASE_REG_SHARE_BOTH', 'KBASE_REG_MEMATTR_INDEX', 'KBASE_REG_MEMATTR_MASK', 'KBASE_REG_MEMATTR_VALUE']:
    constants.append(re.search(r'^#define '+name+r'(?:\([^\n]*?\))?\s+[^\n]+', mem, re.M).group())
for name in ['SHARE_INNER_BITS', 'SHARE_BOTH_BITS']:
    expression = re.search(r'\b'+name+r'\s*=\s*(\([^\n]*?\))', defs).group(1)
    constants.append('#define '+name+' '+expression)
# Assert the allocation path motivating the policy exists in this exact source.
assert re.search(r'u64 flags = BASE_MEM_PROT_CPU_RD[^;]*BASE_MEM_COHERENT_LOCAL', regions)
assert re.search(r'flags & BASE_MEM_COHERENT_LOCAL\).*?KBASE_REG_SHARE_IN', regions, re.S)
pre = '''#define main original_mmu_test_main
#include "nord2_mmu_test.c"
#undef main
typedef uint64_t u64;
'''
post = r'''
static uint64_t raw_leaf(struct arena *a, struct nord2_gpu_mmu *m, uint64_t va) {
 const uint64_t *t=m->root_cpu;
 for(unsigned level=0;level<3;level++) {
  uint64_t e=t[(va>>(39-level*9))&511]; CHECK((e&3)==3,"table type");
  uint64_t pa=e&0xfffffff000ull;
  CHECK(pa>=a->gpu_base && pa-a->gpu_base<a->used,"table bounds");
  t=(const uint64_t*)(a->base+pa-a->gpu_base);
 }
 return t[(va>>12)&511];
}
int main(void) {
 struct arena a={.base=calloc(1,ARENA_BYTES),.gpu_base=0x10000000};
 const struct nord2_gpu_mmu_ops ops={alloc_table,free_table,flush_table,alloc_host,free_host,NULL};
 struct nord2_gpu_mmu m; CHECK(nord2_gpu_mmu_init(&m,&ops,&a,MAX_TABLES),"init");
 uint64_t va=0x100000000ull,pa=0x90000000ull;unsigned cases=0;
 for(unsigned attr=0;attr<=5;attr++)for(unsigned share=0;share<=3;share+=3)
 for(unsigned wr=0;wr<2;wr++)for(unsigned ex=0;ex<2;ex++) {
  if(attr==5 && share)continue;
  unsigned long flags=KBASE_REG_MEMATTR_INDEX(attr)|KBASE_REG_GPU_RD;
  if(wr)flags|=KBASE_REG_GPU_WR;
  if(!ex)flags|=KBASE_REG_GPU_NX;
  if(share)flags|=KBASE_REG_SHARE_IN;
  uint64_t expected=get_mmu_flags(flags)|ENTRY_ACCESS_BIT|ENTRY_IS_ATE_L3;
  CHECK(nord2_gpu_mmu_map_shared(&m,va,pa,16384,attr,share,wr,ex),"map");
  for(unsigned i=0;i<4;i++)CHECK(raw_leaf(&a,&m,va+i*4096)==((pa+i*4096)|expected),"Android raw leaf parity");
  unsigned before=a.flushes;uint64_t pages=m.mapped_pages;
  CHECK(nord2_gpu_mmu_map_shared(&m,va,pa,16384,attr,share,wr,ex),"identical remap");
  CHECK(a.flushes==before && m.mapped_pages==pages,"idempotent");
  CHECK(!nord2_gpu_mmu_map_shared(&m,va-4096,pa-4096,8192,attr,share,!wr,ex),"permission conflict");
  CHECK(nord2_gpu_mmu_range_free(&m,va-4096,4096),"no partial remap");
  CHECK(raw_leaf(&a,&m,va)==(pa|expected),"conflict preserves leaf");
  CHECK(!nord2_gpu_mmu_map_shared(&m,va,pa,16384,attr,share,wr,!ex),"execute conflict");
  CHECK(!nord2_gpu_mmu_map_shared(&m,va,pa,16384,attr,share?0:3,wr,ex),"share conflict");
  CHECK(a.flushes==before && m.mapped_pages==pages,"no failed-remap mutation");
  CHECK(nord2_gpu_mmu_unmap(&m,va,16384),"unmap");cases++;
 }
 unsigned before=a.flushes;size_t used=a.used;
 for(int share=-1;share<5;share++)if(share!=0 && share!=3)
  CHECK(!nord2_gpu_mmu_map_shared(&m,va,pa,4096,0,share,1,0),"invalid SH");
 for(int attr=-1;attr<=8;attr++)if(attr<0 || attr>5)
  CHECK(!nord2_gpu_mmu_map_shared(&m,va,pa,4096,attr,3,1,0),"invalid attribute");
 CHECK(!nord2_gpu_mmu_map_shared(&m,va,pa,4096,5,3,1,0),"uncached sharing rejected like Android");
 CHECK(a.flushes==before && a.used==used && !m.mapped_pages,"invalid arguments atomic");
 CHECK(nord2_gpu_mmu_map(&m,va,pa,4096,0,0,0),"legacy WA mapping");
 CHECK(raw_leaf(&a,&m,va)==(pa|(1ull<<54)|0x4c3),"WA exact nonshareable RO NX flags");
 nord2_gpu_mmu_fini(&m);free(a.base);
 printf("Android unchanged get_mmu_flags: %u cases / %u raw leaves; remap/invalid/WA checks: %d failures\n",cases,cases*4,failures);
 return failures?1:0;
}
'''
out = ROOT/'gpu/build/android-mmu-oracle'
out.mkdir(exist_ok=True)
(out/'oracle.c').write_text(pre+'\n'.join(constants)+'\n'+function+'\n'+post)
cmd = [os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'gpu/include'),'-I'+str(ROOT/'gpu/tests'),str(out/'oracle.c'),str(ROOT/'gpu/src/nord2_gpu_mmu.c'),'-o',str(out/'oracle')]
subprocess.run(cmd,check=True)
subprocess.run([str(out/'oracle')],check=True)
(out/'receipt.json').write_text(json.dumps({'sources':{str(x.resolve()):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths},'function_sha256':hashlib.sha256(function.encode()).hexdigest(),'production_source_sha256':hashlib.sha256((ROOT/'gpu/src/nord2_gpu_mmu.c').read_bytes()).hexdigest(),'oracle_binary_sha256':hashlib.sha256((out/'oracle').read_bytes()).hexdigest(),'compiled':True,'passed':True,'device_tested':False},indent=2)+'\n')
