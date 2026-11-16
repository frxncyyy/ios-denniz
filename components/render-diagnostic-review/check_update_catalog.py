from pathlib import Path
import importlib.util,struct,json,hashlib
p=Path.cwd();base=p/'gpu/build/hardware-20261024'
s=importlib.util.spec_from_file_location('h',p/'../../../../../research/nord2/display-capabilities/hfsx-read.py');h=importlib.util.module_from_spec(s);s.loader.exec_module(h)
s=importlib.util.spec_from_file_location('j',p/'gpu/tools/replay_hfs_journal.py');j=importlib.util.module_from_spec(s);s.loader.exec_module(j)
original=p/'gpu/build/hardware-20261024/offline-install-replayed/metal-rootfs.img'
installed=p/'gpu/build/render-diagnostic-review/hfs-update-rehearsal/metal-rootfs.img'
pages={}
def catalog(path,overlay):
 with path.open('rb') as f:
  def fetch(off,n):
   f.seek(off);v=f.read(n)
   if overlay:
    v=bytearray(v)
    for start in range(off//4096*4096,(off+n+4095)//4096*4096,4096):
     if start in pages:
      a=max(start,off);z=min(start+4096,off+n);v[a-off:z-off]=pages[start][a-start:z-start]
    v=bytes(v)
   return v
  class Reader(h.Reader):
   def get(self,off,n):
    assert off>=0 and n>=0 and off+n<=self.size
    return self.callback(off,n)
  r=Reader(fetch,16<<30,allow_dirty=True);head=r.fread(r.catalog,0,512);leaf=h.U32(head,24);last=0;seen=set();out={}
  while leaf:
   assert leaf not in seen and len(seen)<50000;seen.add(leaf)
   node=r.fread(r.catalog,leaf*r.node_size,14);assert h.U32(node,4)==last
   for key,rec in r.node(leaf,1):
    typ=h.U16(rec,0)
    if typ not in (1,2):continue
    cnid=h.U32(rec,8);assert cnid not in out
    name=bytes().join(struct.pack('>H',c) for c in key[1]).decode('utf-16-be')
    out[cnid]={'parent':key[0],'name':name,'type':typ,'permissions':rec[32:44].hex(),
               'forks':rec[88:248].hex() if typ==2 else None,'finder_type':rec[48:56].hex() if typ==2 else None}
   last,leaf=leaf,h.U32(node,0)
  return out
b=catalog(original,False);a=catalog(installed,False)
def paths(records):
 def path(k):
  if k==2:return ''
  v=records[k];return path(v['parent'])+'/'+v['name']
 return {path(k):v for k,v in records.items()}
bp,ap=paths(b),paths(a)
changed=[k for k in sorted(set(bp)|set(ap)) if bp.get(k)!=ap.get(k)]
allowed=('/System/Library/Extensions/Nord2Metal.bundle/','/usr/local/libexec/nord2-metal-probe','/usr/local/share/nord2-metal/varying.bc','/private/var/db/nord2-metal/')
unknown=[k for k in changed if not any(k.startswith(prefix) for prefix in allowed)]
assert not unknown,unknown
out={'before_records':len(b),'after_records':len(a),'changed_paths':changed,'outside_owned_paths':unknown}
(p/'gpu/build/render-diagnostic-review/catalog-preservation.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'before':len(b),'after':len(a),'changed':len(changed),'outside_owned_paths':unknown}))
