from pathlib import Path
import struct,re,json,hashlib
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists());P=Path(__file__).resolve().parent
b=(R/'research/nord2/wifi/ios-abi/wifid').read_bytes();segs=[];stubs=[];formats=set();uuid=None;symtab=None;indirect=None;n=struct.unpack_from('<I',b,16)[0];o=32
for _ in range(n):
 cmd,sz=struct.unpack_from('<II',b,o)
 if cmd==0x19:
  name,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',b,o+8);segs.append((va,va+fs,fo,name.rstrip(b'\0').decode()))
  for j in range(struct.unpack_from('<I',b,o+64)[0]):
   q=o+72+80*j
   if b[q:q+16].rstrip(b'\0')==b'__stubs':stubs.append(struct.unpack_from('<QQ',b,q+32)+struct.unpack_from('<II',b,q+68))
 if cmd==2:symtab=struct.unpack_from('<IIII',b,o+8)
 if cmd==0xb:indirect=struct.unpack_from('<II',b,o+56)
 if cmd==0x1b:uuid=b[o+8:o+24].hex()
 if cmd==0x80000034:
  fix=struct.unpack_from('<I',b,o+8)[0];starts=fix+struct.unpack_from('<I',b,fix+4)[0];count=struct.unpack_from('<I',b,starts)[0]
  for j in range(count):
   rel=struct.unpack_from('<I',b,starts+4+4*j)[0]
   if rel:formats.add(struct.unpack_from('<H',b,starts+rel+6)[0])
 o+=sz
assert formats=={6},formats

def read(va,n):
 for lo,hi,off,_ in segs:
  if lo<=va and va+n<=hi:return b[off+va-lo:off+va-lo+n]
 raise ValueError(hex(va))
def cstr(a):return read(a,512).split(b'\0')[0].decode('ascii',errors='replace')
def cfstr(a):
 d=read(a,32)
 if struct.unpack_from('<Q',d,8)[0]!=0x7c8:raise ValueError('not selected ASCII CFString')
 ptr,n=struct.unpack_from('<QQ',d,16)
 if n>1024:raise ValueError('CFString length')
 if ptr>>51:ptr=0x100000000+(ptr&0xfffffffff) # selected DYLD_CHAINED_PTR_64_OFFSET rebases
 return read(ptr,n).decode('ascii',errors='replace')
lines=(R/'research/nord2/wifi/ios-service-bridge/initial-query-audit/wifid-disassembly.txt').read_text().splitlines();imps=json.loads((R/'research/nord2/wifi/ios-service-bridge/service-compat-resume/scan-requests/wifid-imports.json').read_text());rev={v:k for k,v in imps.items()}
wanted={a:n for n,a in imps.items() if n.startswith(('_MG','_IORegistry','_IOServiceMatching','_IOServiceNameMatching','_IOServiceGetMatchingService'))}
# Resolve the selected import map against original LC_SYMTAB/indirect stubs.
verified_imports={}
for va,size,start,stride in stubs:
 for i in range(size//stride):
  idx=struct.unpack_from('<I',b,indirect[0]+4*(start+i))[0]
  if idx&0xc0000000:continue
  assert idx<symtab[1]
  off=struct.unpack_from('<I',b,symtab[0]+16*idx)[0];end=b.index(0,symtab[2]+off);name=b[symtab[2]+off:end].decode()
  verified_imports[hex(va+i*stride)]=name
for addr,name in wanted.items():assert verified_imports.get(addr)==name,(addr,name,verified_imports.get(addr))
records=[]
for i,l in enumerate(lines):
 m=re.match(r'([0-9a-f]+): ((?:[0-9a-f]{2} ){4})\s*(.*)',l)
 if not m:continue
 ins=m[3];call=re.search(r'\bbl\s+(0x[0-9a-f]+)',ins)
 if not call or call[1] not in wanted:continue
 va=int(m[1],16);r={'call':hex(va),'target':wanted[call[1]],'constants':{}};regs={}
 for old in lines[max(0,i-18):i]:
  q=re.match(r'([0-9a-f]+): ((?:[0-9a-f]{2} ){4})\s*(.*)',old)
  if not q:continue
  v=int(q[1],16);assert read(v,4)==bytes.fromhex(q[2]);s=q[3]
  q=re.match(r'adrp\s+x(\d+), (0x[0-9a-f]+)',s)
  if q:regs[q[1]]=int(q[2],16);continue
  q=re.match(r'add\s+x(\d+), x(\d+), #(\d+)',s)
  if q:regs[q[1]]=regs.get(q[2],0)+int(q[3]) if q[2] in regs else None;continue
  q=re.match(r'adr\s+x(\d+), #(-?\d+)',s)
  if q:regs[q[1]]=v+int(q[2]);continue
  q=re.match(r'mov\s+[wx](\d+), #(-?\d+)',s)
  if q:regs[q[1]]=int(q[2]);continue
  q=re.match(r'mov\s+x(\d+), x(\d+)',s)
  if q:regs[q[1]]=regs.get(q[2]);continue
  if re.match(r'(bl|blr)\b',s):
   for k in map(str,range(19)):regs.pop(k,None)
  else:
   q=re.match(r'(?:ldr|ldur|sub|and|orr|csel)\s+[wx](\d+)',s)
   if q:regs.pop(q[1],None)
 for reg,a in regs.items():
  if a is None or a<0x100000000 or int(reg)>5:continue
  v={'va':hex(a)}
  try:v['cfstring']=cfstr(a)
  except (ValueError,struct.error):
   try:v['cstring']=cstr(a)
   except ValueError:pass
  r['constants']['x'+reg]=v
 records.append(r)
 lo=max(0,i-22);hi=min(len(lines),i+31);out=[]
 for old in lines[lo:hi]:
  q=re.match(r'([0-9a-f]+): ((?:[0-9a-f]{2} ){4})',old)
  if q:assert read(int(q[1],16),4)==bytes.fromhex(q[2])
  out.append(old)
 (P/(hex(va)+'-'+wanted[call[1]][1:]+'.txt')).write_text('\n'.join(out)+'\n')
(P/'callsites.json').write_text(json.dumps({'binary_sha256':hashlib.sha256(b).hexdigest(),'uuid':uuid,'chained_pointer_formats':sorted(formats),'calls':records},indent=2)+'\n')
if __name__=='__main__':
 for x in records:print(x)

# Additional bounded semantic branches, all raw instruction bytes verified.
ranges=[(0x100045a68,0x100045be0),(0x1000467c4,0x1000467cc),(0x100062da0,0x100062de4),(0x1000665a4,0x1000665ec),(0x10006679c,0x100066830),(0x1000902bc,0x1000903e4),(0x10009b4c8,0x10009b540),(0x10009b640,0x10009b690),(0x1000fa51c,0x1000fa584)]
extra=[];count=0
for lo,hi in ranges:
 extra.append('RANGE '+hex(lo)+'..'+hex(hi))
 for l in lines:
  m=re.match(r'([0-9a-f]+): ((?:[0-9a-f]{2} ){4})',l)
  if m and lo<=int(m[1],16)<hi:
   assert read(int(m[1],16),4)==bytes.fromhex(m[2]);extra.append(l);count+=1
(P/'extra-wifid-branches.txt').write_text('\n'.join(extra)+'\n')
(P/'verification.json').write_text(json.dumps({'exact_selected_binary_sha256':hashlib.sha256(b).hexdigest(),'uuid':uuid,'direct_calls':len(records),'import_names_verified_from_original_indirect_symbols':True,'MG_direct_calls':sum(r['target'].startswith('_MG') for r in records),'registry_matching_direct_calls':sum(not r['target'].startswith('_MG') for r in records),'extra_instructions_verified':count,'chained_pointer_formats':sorted(formats),'dynamic_key_04685c':'InternalBuild, proven x26 setup at0467c4/7c8','runtime':False,'REA_tools_available':False,'method':'ordinary offline MachO reads and existing saved disassembly; not REA Evidence'},indent=2)+'\n')
