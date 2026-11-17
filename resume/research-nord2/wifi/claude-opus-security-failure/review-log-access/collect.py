#!/usr/bin/env python3
"""Bounded read-only HFSX Wi-Fi crash collection; never boots/mounts/repairs.
ADB mode is for a separately authorized, already running verified RAM rescue.
Default reads only wifid crash files; --related adds wifip2pd/WiFiAgent/Preferences. Jetsam and diagnosticd stats
are metadata only; no unrelated file contents or raw catalog pages exported.
"""
import argparse,hashlib,importlib.util,json,re,struct,subprocess,time
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists())
BASE=ROOT/'research/nord2/display-capabilities/hfsx-read.py'
assert hashlib.sha256(BASE.read_bytes()).hexdigest()=='18454ae1c1dbd6fbb5526a4f6b9ea8ca140e5a7bfa5e29a489e4578682f469d1'
spec=importlib.util.spec_from_file_location('bounded_hfsx',BASE);h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
CRASH='private/var/mobile/Library/Logs/CrashReporter'
STATS='private/var/log/diagnosticd/stats'
ROOT_BYTES=17179869184
MAX_LEAVES=256
MAX_ENTRIES=4096
MAX_FILES=8
MAX_FILE_BYTES=4*1024*1024
MAX_DATA_BYTES=32*1024*1024
MAX_READ_BYTES=36*1024*1024
FILE_RE=re.compile(r'(?:wifid|wifip2pd|WiFiAgent|Preferences)(?:[-_.][A-Za-z0-9_.-]+)?\.(?:ips|crash|panic|log)(?:\.synced)?\Z')
JET_RE=re.compile(r'JetsamEvent[-_][A-Za-z0-9_.-]+\.(?:ips|crash)(?:\.synced)?\Z')
class Reader(h.Reader):
 def get(self,off,n):
  if off<0 or n<0 or off+n>self.size:raise h.Refusal('physical bounds')
  out=bytearray()
  while n:
   page=off//4096*4096;at=off-page;count=min(n,4096-at)
   if page not in self.cache:
    if self.read_bytes+4096>MAX_READ_BYTES:raise h.Refusal('physical read budget')
    raw=self.callback(page,4096)
    if len(raw)!=4096:raise h.Refusal('short physical read')
    self.cache[page]=raw;self.read_bytes+=4096
   out.extend(self.cache[page][at:at+count]);off+=count;n-=count
  return bytes(out)
 def read_log(self,path):
  rec=self.resolve(path)
  if rec is None:return {'path':path,'found':False},b''
  if len(rec)!=248 or h.U16(rec,0)!=2:raise h.Refusal('not ordinary file')
  mode=h.U16(rec,42)
  if mode&0xf000!=0x8000 or rec[41]&0x20 or rec[48:52] in (b'hlnk',b'fdrp'):raise h.Refusal('symlink/compressed/hardlink/special file refused')
  fork=self.fork(rec,88)
  if fork[0]>MAX_FILE_BYTES:raise h.Refusal('4MiB file cap')
  raw=self.fread(fork,0,fork[0])
  return {'path':path,'found':True,'cnid':h.U32(rec,8),'uid':h.U32(rec,32),'gid':h.U32(rec,36),'mode':mode,'content_modify_time_hfs':h.U32(rec,16),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'dirty_volume_observation':self.dirty},raw
 def resolve(self,path):
  parts=path.split('/');parent=2
  if any(not x or x in ('.','..') or not x.isascii() for x in parts):raise h.Refusal('nonexact path')
  for i,name in enumerate(parts):
   rec=self.lookup(parent,name)
   if rec is None:return None
   if i<len(parts)-1:
    if len(rec)!=88 or h.U16(rec,0)!=1 or h.U16(rec,42)&0xf000!=0x4000:raise h.Refusal('nonordinary directory')
    parent=h.U32(rec,8)
  return rec
 def children(self,path):
  rec=self.resolve(path)
  if rec is None:return None
  if len(rec)!=88 or h.U16(rec,0)!=1 or h.U16(rec,42)&0xf000!=0x4000:raise h.Refusal('listing nonordinary directory')
  parent=h.U32(rec,8);key=(parent,());num=self.root
  for height in range(self.depth,1,-1):
   rows=self.node(num,height);possible=[v for k,v in rows if k<=key]
   if not possible:return []
   if len(possible[-1])!=4:raise h.Refusal('index child size')
   num=h.U32(possible[-1],0)
  seen=set();out=[];matched=0;last=None;previous=None
  for _ in range(MAX_LEAVES):
   if num in seen:raise h.Refusal('leaf cycle')
   seen.add(num);rows=self.node(num,1);raw=self.fread(self.catalog,num*self.node_size,14)
   if previous is not None and h.U32(raw,4)!=previous:raise h.Refusal('leaf backlink')
   if rows and last is not None and rows[0][0]<=last:raise h.Refusal('leaf ordering')
   for k,v in rows:
    if k[0]>parent:return out
    if k[0]!=parent or not k[1]:continue # preceding keys or folder thread
    matched+=1
    if matched>MAX_ENTRIES:raise h.Refusal('directory entry cap')
    if not all(0<x<128 for x in k[1]):continue
    name=''.join(map(chr,k[1]))
    if '/' in name or name in ('.','..'):raise h.Refusal('invalid child name')
    out.append((name,v))
   if rows:last=rows[-1][0]
   nxt=h.U32(raw,0)
   if not nxt:return out
   previous,num=num,nxt
  raise h.Refusal('leaf traversal cap')

def metadata(name,rec):
 if len(rec)<44:raise h.Refusal('short catalog entry')
 return {'name':name,'record_type':h.U16(rec,0),'cnid':h.U32(rec,8),'mode':h.U16(rec,42),'content_modify_time_hfs':h.U32(rec,16)}

def collect(reader,output,fetch,related=False):
 result={'writes':0,'mounts':0,'journal_replay':False,'volume_dirty':reader.dirty,'trusted_clean_observation':not reader.dirty,'files':[],'directories':[],'jetsam_candidates':[],'stats_metadata':None}
 selected=[]
 for directory in [CRASH,CRASH+'/Retired']:
  try:rows=reader.children(directory)
  except (h.Refusal,struct.error) as exc:
   result['files'].append({'directory':directory,'error':str(exc)});continue
  if rows is None:
   result['directories'].append({'directory':directory,'found':False});continue
  result['directories'].append({'directory':directory,'found':True,'entry_count':len(rows)})
  for name,rec in rows:
   if FILE_RE.fullmatch(name) and (related or name.startswith(('wifid-','wifid_','wifid.'))):selected.append((directory+'/'+name,metadata(name,rec)))
   elif JET_RE.fullmatch(name):result['jetsam_candidates'].append({'directory':directory,**metadata(name,rec),'contents_read':False})
 # Lexical filename only: RTC epoch and mtime are untrusted. No chronology claim.
 selected.sort(key=lambda x:(x[0].rsplit('/',1)[1],not x[0].startswith(CRASH+'/Retired/')),reverse=True)
 result['matched_report_count']=len(selected);result['truncated_selection']=len(selected)>MAX_FILES
 total_data=0
 for path,meta in selected[:MAX_FILES]:
  try:
   row,raw=reader.read_log(path)
   if len(raw)>MAX_DATA_BYTES-total_data:raise h.Refusal('32MiB report cap')
   total_data+=len(raw)
   if row.get('found'):
    name='report-%02d.bin'%len([v for v in result['files'] if 'saved_as' in v]);(output/name).write_bytes(raw);row['saved_as']=name
   result['files'].append(row)
  except (h.Refusal,struct.error) as exc:result['files'].append({'path':path,'error':str(exc)})
 # Only inspect exact stats entry metadata. Unknown aggregate files are not read.
 try:
  rec=reader.resolve(STATS)
  if rec is None:result['stats_metadata']={'found':False,'contents_read':False}
  elif h.U16(rec,0)==1:
   rows=reader.children(STATS);result['stats_metadata']={'is_directory':True,'related_entries':[metadata(n,v) for n,v in rows if any(x in n for x in ['wifid','wifip2pd','WiFiAgent','Preferences'])],'contents_read':False}
  else:result['stats_metadata']={**metadata('stats',rec),'contents_read':False}
 except (h.Refusal,struct.error) as exc:result['stats_metadata']={'error':str(exc),'contents_read':False}
 if fetch(0,4096)!=reader.cache[0]:raise h.Refusal('volume header changed')
 result['selection_order']='descending filename; not trusted chronology';result['payload_bytes']=total_data
 result['header_stable']=True;result['read_bytes']=reader.read_bytes+4096
 result['read_pages']=[{'offset':k,'bytes':len(v),'sha256':hashlib.sha256(v).hexdigest()} for k,v in sorted(reader.cache.items())]
 (output/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

def main():
 ap=argparse.ArgumentParser(description=__doc__);m=ap.add_mutually_exclusive_group(required=True);m.add_argument('--image',type=Path);m.add_argument('--adb',type=Path);ap.add_argument('--serial');ap.add_argument('--output',type=Path,required=True);ap.add_argument('--allow-dirty-observation',action='store_true');ap.add_argument('--related',action='store_true',help='Also allow wifip2pd/WiFiAgent/Preferences reports');a=ap.parse_args()
 if a.adb and not a.serial:ap.error('--serial required for already-authorized rescue')
 if a.serial and not a.adb:ap.error('--serial only valid with --adb')
 a.output.mkdir(parents=True,exist_ok=False,mode=0o700);deadline=time.monotonic()+120
 def bound():
  n=deadline-time.monotonic()
  if n<=0:raise h.Refusal('collection120s deadline')
  return min(30,n)
 if a.image:
  file=a.image.open('rb');assert a.image.stat().st_size==ROOT_BYTES
  def fetch(off,n):bound();file.seek(off);return file.read(n)
 else:
  # Import only the existing read-only preflight, never its write/verify main.
  preflight_path=ROOT/'tools/nord2-install-root.py'
  if hashlib.sha256(preflight_path.read_bytes()).hexdigest()!='0addf55b16acc5c1476286cf09e147648b02d9360492f66ed12531018b867be1':raise h.Refusal('preflight source drift')
  spec=importlib.util.spec_from_file_location('root_preflight',preflight_path);pre=importlib.util.module_from_spec(spec);spec.loader.exec_module(pre)
  original_shell=pre.shell
  pre.shell=lambda adb,serial,command,timeout=30:original_shell(adb,serial,command,timeout=min(timeout,bound()))
  check=pre.preflight(str(a.adb),a.serial);(a.output/'preflight.json').write_text(json.dumps(check,indent=2)+'\n')
  def fetch(off,n):
   if off%4096 or n!=4096 or off<0 or off+n>ROOT_BYTES:raise h.Refusal('transport bounds')
   cmd=f'dd if=/dev/block/sdc57 bs=4096 skip={off//4096} count=1 2>/dev/null'
   p=subprocess.run([str(a.adb),'-s',a.serial,'shell','-T',cmd],capture_output=True,check=True,timeout=bound())
   if p.stderr or len(p.stdout)!=4096:raise h.Refusal('read transport status/length')
   return p.stdout
 try:
  reader=Reader(fetch,ROOT_BYTES,a.allow_dirty_observation);result=collect(reader,a.output,fetch,a.related)
  if a.adb:pre.preflight(str(a.adb),a.serial) # same unmounted identity after reads
 except Exception as exc:
  error=str(exc) if isinstance(exc,h.Refusal) else 'read-only collection failure: '+type(exc).__name__
  (a.output/'failure.json').write_text(json.dumps({'error':error,'writes':0,'mounts':0,'journal_replay':False})+'\n');raise SystemExit('collection refused; see private failure.json') from None
 finally:
  if a.image:file.close()
 print(json.dumps({'files_saved':sum('saved_as' in x for x in result['files']),'read_bytes':result['read_bytes'],'volume_dirty':reader.dirty,'writes':0,'mounts':0}))
if __name__=='__main__':
 try:main()
 except SystemExit:raise
 except Exception as exc:raise SystemExit('read-only preflight/collection refused ('+type(exc).__name__+'); no transport identity printed') from None
