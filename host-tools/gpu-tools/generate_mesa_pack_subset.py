#!/usr/bin/env python3
"""Keep Mesa's generated v9 enums, types and packing macros verbatim.
No unpack/printing/libmesa dependencies; output is a private build artifact.
"""
from pathlib import Path
import re,hashlib
r=Path(__file__).resolve().parents[1]
s=(r/'build/mesa-build/src/panfrost/genxml/v9_pack.h').read_text()
items=[]
# Balanced braces include nested aggregate/union structs in full.
pattern=r'^enum \w+ \{|^struct \w+ \{|^#define (?:[^\n]*\\\n)*[^\n]*'
for m in re.finditer(pattern,s,re.M):
 t=m.group()
 if t.startswith('#define'):
  name=t.split()[1].split('(')[0]
  if not name.startswith('MALI_') or name.endswith(('_unpack','_print')):continue
 else:
  end=m.end();depth=1
  while depth:
   c=s[end];depth+=(c=='{')-(c=='}');end+=1
  assert s[end]==';'
  t=s[m.start():end+1]
 items.append(t)
head='''/* Generated from Mesa81daea66703eeea1a232446e3a36902353fc3045 v9.
 * Copyright2024 Collabora Ltd. SPDX-License-Identifier: MIT
 * Layout macros below are unchanged. See generate_mesa_pack_subset.py. */
#ifndef NORD2_MESA_PACK_H
#define NORD2_MESA_PACK_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define ALIGN_POT(v,a) (((v)+(a)-1)&~((a)-1))
#define IS_POT_NONZERO(x) ((x)&&!((x)&((x)-1)))
static inline unsigned util_logbase2(unsigned x){return 31u-__builtin_clz(x);}
static inline uint64_t util_bitpack_uint(uint64_t v,unsigned s,unsigned e){
 assert(e==63 || !(v>>(e-s+1)));return v<<s;}
static inline uint64_t util_bitpack_sint(int64_t v,unsigned s,unsigned e){
 unsigned n=e-s+1;return ((uint64_t)v & (n==64?~0ull:((1ull<<n)-1)))<<s;}
static inline uint32_t util_bitpack_float(float f){union{float f;uint32_t u;}v={.f=f};return v.u;}
'''
out=r/'build/tegu-frontend/nord2_mesa_pack.h';out.parent.mkdir(exist_ok=True,parents=True)
out.write_text(head+'\n\n'.join(items)+'\n#endif\n')
print(len(items),len(out.read_bytes()),hashlib.sha256(s.encode()).hexdigest())
