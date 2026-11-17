#!/usr/bin/env python3
"""Offline common-WiFi transaction evidence; no device operations."""
import argparse,base64,hashlib,json,re
from pathlib import Path
PREFIX='NORD2 WIFI COMMON'
DIRTY='NORD2 WIFI COMMON: dirty restoration; retain power/ownership; refuse handoff'
SCHEMAS={
 'COLD':('NORD2 WIFI COMMON COLD:', 'status valid cpu top power'),
 'RESULT':('NORD2 WIFI COMMON:', 'status phase snapshots verified error restore_error clean'),
 'RESTORE':('NORD2 WIFI COMMON RESTORE:', 'touched restored valid field'),
 'TIME':('NORD2 WIFI COMMON TIME:', 'ticks frequency'),
 'OUTER':('NORD2 WIFI: power probe', 'stage id cfg success'),
 'POWER':('NORD2 WIFI: power probe', 'restored'),
 'SKIP_GUARD':('NORD2 WIFI COMMON:', 'skipped valid field raw')}
FULL=(1<<40)-1

def parse(data):
 text=data.decode('utf-8',errors='replace');issues=[];records={};evidence=[];order=[]
 try:data.decode('utf-8')
 except UnicodeDecodeError:issues.append('invalid UTF8')
 def save(kind,value,line,num):
  if kind in records:issues.append('duplicate '+kind)
  records[kind]=value;order.append(kind);evidence.append({'line':num,'kind':kind,'raw':line})
 for num,line in enumerate(text.splitlines(keepends=True),1):
  relevant='NORD2 WIFI COM' in line or 'NORD2 WIFI: power pro' in line
  if not relevant:continue
  if not line.endswith('\n'):issues.append('unterminated report line')
  if line.count('NORD2 WIFI')!=1:issues.append('interleaved WiFi report markers')
  body=line[line.index('NORD2 WIFI'):].rstrip('\r\n')
  if len(body.encode())>=255:issues.append('possible truncated report')
  if DIRTY in body or 'NORD2 WIFI COMMON: dirty; refuse another probe' in body:
   # Panic formatting may append platform location text, preserve it verbatim.
   save('DIRTY',{'panic_observed':True},line,num);continue
  if body.startswith('NORD2 WIFI COMMON: skipped=1 reason='):
   reason=body.split('reason=',1)[1]
   if reason not in ('owner-or-AP','rgu-map','table-map'):issues.append('unknown/malformed skip reason')
   save('SKIP',{'reason':reason},line,num);continue
  matched=False
  for kind,(prefix,fields) in SCHEMAS.items():
   if not body.startswith(prefix):continue
   keys=fields.split();pattern=re.escape(prefix)+r' '+r' '.join(re.escape(k)+r'=(-?(?:0x[0-9a-fA-F]+|[0-9]+))' for k in keys)
   m=re.fullmatch(pattern,body)
   if not m:continue
   matched=True;vals={k:int(v,16 if v.lstrip('-').startswith('0x') else 10) for k,v in zip(keys,m.groups())}
   for k,v in vals.items():
    signed=k in ('status','error','restore_error')
    bits=64 if k in ('ticks','frequency','touched','restored') else 32
    if not (-(1<<31)<=v<(1<<31) if signed else 0<=v<(1<<bits)):issues.append('numeric bounds '+kind+'.'+k)
   save(kind,vals,line,num);break
  if not matched:issues.append('malformed/unknown common report line')
 classification='unknown';success=False
 cold=records.get('COLD');result=records.get('RESULT');restore=records.get('RESTORE');timing=records.get('TIME');outer=records.get('OUTER');power=records.get('POWER');skip=records.get('SKIP') or records.get('SKIP_GUARD');dirty='DIRTY' in records
 if cold:
  mask=cold['valid'];values=[cold[k] for k in ('cpu','top','power')]
  if mask not in (0,1,3,7) or any(v and not(mask>>i&1) for i,v in enumerate(values)):issues.append('cold invalid mask/unread values')
  isc=not(values[0]&1 or values[1]&0x80 or values[2]&4)
  if cold['status']==0 and (mask!=7 or not isc):issues.append('cold success contradicts raw state')
  elif cold['status']==1 and (mask!=7 or isc):issues.append('cold noncold contradicts raw state')
  elif cold['status']==-1 and mask==7:issues.append('cold failure after complete reads')
  elif cold['status'] not in (-1,0,1):issues.append('unknown cold status')
 if 'SKIP_GUARD' in records:
  g=records['SKIP_GUARD'];n=g['valid'].bit_count();f=g['field'];masks=[1,0x80,4,0x80,1,0x3000000,0x1000003,1,1,1,1,1,1,0x3e]
  if g['skipped']!=1 or g['valid']>0x3fff or g['valid']&(g['valid']+1) or f>14:issues.append('invalid skipped predicate mask')
  elif f==14:
   if n or g['raw']:issues.append('invalid AP-only skipped guard')
  elif n==f:
   if g['raw']:issues.append('unread skipped guard raw is nonzero')
  elif n==f+1:
   if not(g['raw']&masks[f]):issues.append('skipped predicate raw passes')
  else:issues.append('skipped predicate index/mask disagreement')
 if power and power['restored'] not in (0,1):issues.append('invalid outer restoration bool')
 if outer and outer['success'] not in (0,1):issues.append('invalid outer success bool')
 if timing and not timing['frequency']:issues.append('zero timer frequency')
 if result:
  if skip or not cold or cold['status']!=0 or not restore or not timing:issues.append('transaction missing cold/restore/time or conflicting skip')
  if result['status'] not in (-1,0) or result['clean'] not in (0,1) or result['snapshots']>40 or result['verified']>40:issues.append('invalid transaction result bounds')
  if restore:
   touched=restore['touched'];restored=restore['restored'];count=touched.bit_count()
   if touched>FULL or restored&~touched or touched&(touched+1):issues.append('invalid touched/restored masks')
   if bool(result['clean'])!=(touched==restored):issues.append('clean disagrees with restored bits')
   if result['verified']>count or (count and result['snapshots']!=40):issues.append('transaction counts disagree with mutation')
   if restore['valid']>0x3fff or restore['valid']&(restore['valid']+1) or restore['field']>14:issues.append('invalid final guard evidence')
  if result['error'] not in (0,-1,-2,-3,-4,-10,-11,-12) or result['restore_error'] not in (0,-5,-6):issues.append('unknown transaction error code')
  if result['status']!=0:
   if result['phase']==0 and result['error']==-12:
    if result['snapshots'] or result['verified'] or result['restore_error'] or not result['clean'] or (restore and restore['touched']):issues.append('invalid prepare refusal')
   elif result['phase']==2 and result['error']==-10:
    if result['snapshots']!=40 or result['verified']!=40 or result['clean'] or result['restore_error'] or (restore and (restore['touched']!=FULL or restore['restored'])):issues.append('invalid inner dirty retention')
   elif result['phase']==1:
    if result['error']!=-2 or result['restore_error'] or result['snapshots']>=40 or result['verified'] or (restore and restore['touched']):issues.append('invalid snapshot-failure shape')
   elif result['phase']==3:
    if result['snapshots']!=40 or (not result['error'] and not result['restore_error']):issues.append('invalid mutation/restore-failure shape')
   else:issues.append('invalid failed transaction phase')
  if result['status']==0:
   if any(result[k] for k in ('error','restore_error')) or result['phase']!=4 or result['snapshots']!=40 or result['verified']!=40 or result['clean']!=1:issues.append('success lacks complete clean transaction')
   if restore and (restore['touched']!=FULL or restore['restored']!=FULL or restore['valid']!=0x3fff or restore['field']!=14):issues.append('success lacks all40restored/finalguard')
  elif not(result['error'] or result['restore_error']):issues.append('failure without error')
  if not result['clean']:
   if not result['restore_error'] and result['error']!=-10:issues.append('dirty state without restore error')
   classification='dirty_halt' if dirty else 'dirty_state_halt_not_observed'
   if power or outer:issues.append('dirty state conflicts with outer continuation')
  else:
   if dirty:issues.append('clean transaction conflicts with dirty panic')
   if not power or power['restored']!=1:issues.append('missing successful outer power restoration')
   if not outer or outer['stage']!=3 or outer['id']!=0x20010101 or outer['success']!=1:issues.append('missing outer window result')
   classification='clean_success' if result['status']==0 else 'clean_failed_transaction'
 else:
  if restore or timing:issues.append('restore/time without transaction')
  if dirty:
   classification='dirty_halt'
   if power or outer or skip:issues.append('dirty panic conflicts with continuation')
  elif skip or cold:
   classification='skipped_guard' if skip else 'cold_guard_refused'
   if skip and cold and cold['status']!=0:issues.append('skip after failed cold snapshot')
   if not outer or outer['stage']!=3 or outer['id']!=0x20010101 or outer['success']!=1:issues.append('guard refusal missing outer window result')
   if cold and cold['status']==0 and not skip:issues.append('cold passed but transaction absent')
   if not power or power['restored']!=1:issues.append('guard refusal missing outer restoration')
   if skip and skip.get('reason') in ('owner-or-AP','rgu-map') and cold:issues.append('early skip conflicts with cold reads')
  else:classification='common_not_observed'
 # Canonical chronological flow; duplicates separately rejected above.
 rank={'COLD':0,'SKIP':1,'SKIP_GUARD':1,'RESULT':1,'RESTORE':2,'TIME':3,'DIRTY':4,'OUTER':5,'POWER':6}
 if any(rank[a]>rank[b] for a,b in zip(order,order[1:])):issues.append('conflicting report order')
 if 'SKIP' in records and 'SKIP_GUARD' in records:issues.append('multiple skip forms')
 complete=not issues
 success=complete and classification=='clean_success'
 return {'complete':complete,'success':success,'classification':classification if complete else 'incomplete_or_conflicting','issues':issues,'records':records,'raw_evidence':evidence,'raw_text':text,'raw_base64':base64.b64encode(data).decode(),'source_sha256':hashlib.sha256(data).hexdigest(),'elapsed_seconds':timing['ticks']/timing['frequency'] if timing and timing['frequency'] and complete else None,'limits':['No common lines means not observed; absence alone does not prove opt-out.','Clean success reports40register writes/readbacks and verified restoration plus outer power restoration only.','No firmware execution, networking, visible GUI or performance outcome is inferred.']}
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);args=ap.parse_args();r=parse(args.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
