#!/usr/bin/env python3
"""Offline frozen live-x2/common-AP parser. Never operates a device."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('common_base',HERE/'common-base.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
NUM=r'(-?(?:0x[0-9a-fA-F]+|[0-9]+))'
def nums(body,prefix,keys):
 m=re.fullmatch(re.escape(prefix)+' '+ ' '.join(re.escape(k)+'='+NUM for k in keys.split()),body)
 if not m:return None
 return {k:int(v,16 if v.lstrip('-').startswith('0x') else 10) for k,v in zip(keys.split(),m.groups())}
def common_ap(data):
 lines=data.decode('utf8',errors='replace').splitlines(keepends=True);kept=[];ap={};issues=[]
 for n,line in enumerate(lines,1):
  if 'NORD2 WIFI COMMON AP' not in line:kept.append(line);continue
  body=line[line.index('NORD2 WIFI COMMON AP'):].rstrip('\r\n')
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:issues.append('malformed/truncated AP line')
  kind='RAW' if body.startswith('NORD2 WIFI COMMON AP RAW:') else 'AP'
  prefix='NORD2 WIFI COMMON AP RAW:' if kind=='RAW' else 'NORD2 WIFI COMMON AP:'
  fields='pwr ack0 ack1 gals prot rx host' if kind=='RAW' else 'owner_ok owner_fail valid failed'
  value=nums(body,prefix,fields)
  if value is None or any(not 0<=v<2**32 for v in (value or {}).values()):issues.append('malformed AP fields');continue
  if kind in ap:issues.append('duplicate AP '+kind)
  ap[kind]=dict(value=value,line=n)
 r=base.parse(''.join(kept).encode());issues+=r['issues']
 if bool(ap.get('AP'))!=bool(ap.get('RAW')):issues.append('incomplete AP pair')
 if len(ap)==2:
  a=ap['AP']['value'];raw=ap['RAW']['value'];values=list(raw.values());valid=a['valid'];f=a['failed'];owner=a['owner_fail']
  if ap['AP']['line']>=ap['RAW']['line']:issues.append('AP pair order')
  if owner:
   if not(1<=owner<=9) or a['owner_ok']!=(1<<(owner-1))-1 or valid or f!=7 or any(values):issues.append('owner failure prefix/mask/raw contradiction')
  else:
   masks=[31,2,2,1,0x6000,4,0x20];wants=[13,2,2,1,0,0,0]
   if a['owner_ok']!=0x1ff or f>6 or valid!=(1<<(f+1))-1:issues.append('AP firstfailure mask/prefix contradiction')
   else:
    if any((values[i]&masks[i])!=wants[i] for i in range(f)) or (values[f]&masks[f])==wants[f]:issues.append('AP raw does not demonstrate firstfailure')
    if any(values[f+1:]):issues.append('unread AP raw is nonzero')
  if r['classification']=='clean_success':issues.append('clean success contradicts captured AP failure')
  outer_lines=[i for i,x in enumerate(lines,1) if 'NORD2 WIFI: power probe stage=' in x]
  if outer_lines and ap['RAW']['line']>=outer_lines[0]:issues.append('AP observation after outer continuation')
 if r['records'].get('SKIP',{}).get('reason')=='owner-or-AP' and len(ap)!=2:issues.append('new AP skip lacks observability (legacy evidence is insufficient)')
 r.update(ap_records=ap,issues=issues,complete=not issues,success=not issues and r['success'])
 return r

def live(data):
 text=data.decode('utf8',errors='replace');records={};issues=[];order=[];evidence=[]
 schemas={'POLICY':('NORD2 UFS X2 LIVE POLICY:','armed invalid return_blocked'),
 'RESULT':('NORD2 UFS X2 LIVE:','rc phase error active uncertain root_allowed handoff_allowed'),
 'PROOF':('NORD2 UFS X2 LIVE PROOF:','commands sets reads verified rollback original'),
 'REFUSED':('NORD2 UFS X2 LIVE REFUSED:','error root_allowed return_blocked manual_reset'),
 'STOP':('NORD2 UFS X2 STOP:','rc phase error commands sets reads'),
 'STATE':('NORD2 UFS X2 STATE:','uncertain rollback original engine_root engine_handoff'),
 'LAST':('NORD2 UFS X2 LAST:','attr result is hcs root_published return_blocked')}
 runtime='NORD2 UFS X2 LIVE: runtime I/O failed; retain DMA/ownership; manual reset required'
 uncertain='NORD2 UFS X2 LIVE: uncertain before I/O; retain DMA/ownership; manual reset required'
 for n,line in enumerate(text.splitlines(keepends=True),1):
  if not any(x in line for x in ('NORD2 UFS X2 LIVE','NORD2 UFS X2 STOP:','NORD2 UFS X2 STATE:','NORD2 UFS X2 LAST:')):continue
  start=line.index('NORD2 UFS X2');body=line[start:].rstrip('\r\n')
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 UFS X2')!=1:issues.append('truncated/interleaved live record')
  kind=None;value=None
  if body.startswith(runtime) or body.startswith(uncertain):kind='RUNTIME';value={'panic_observed':True,'kind':'uncertain_before_io' if body.startswith(uncertain) else 'io_failure'}
  elif body=='NORD2 UFS X2 LIVE PROFILE: link_fields=pre-transition-baseline return_policy=manual-reset-only':kind='PROFILE';value={'baseline_only':True}
  else:
   for k,(prefix,fields) in schemas.items():
    b=body
    if k=='POLICY':
     suffix=' reason=manual-reset-required'
     if not b.endswith(suffix):continue
     b=b[:-len(suffix)]
    v=nums(b,prefix,fields)
    if v is not None:kind=k;value=v;break
  if not kind:issues.append('malformed/unknown live record');continue
  if kind in records:issues.append('duplicate live '+kind)
  records[kind]=value;order.append(kind);evidence.append({'line':n,'kind':kind,'raw':line})
  for key,v in value.items():
   if type(v) is int and not(-(2**31)<=v<2**31 if key in ('rc','error') else 0<=v<2**32):issues.append('live numeric bounds')
 p=records.get('POLICY');r=records.get('RESULT');proof=records.get('PROOF');ref=records.get('REFUSED');classification='not_observed'
 if p and(p['armed']!=1 or p['invalid'] not in (0,1) or p['return_blocked']!=1):issues.append('invalid permanent-return policy')
 if records and not p:issues.append('missing live armed policy')
 if ref:
  classification='refused_manual_reset'
  if not ref['error'] or ref['root_allowed'] or ref['return_blocked']!=1 or ref['manual_reset']!=1 or r or proof:issues.append('refused conflicts with result/root')
 elif r:
  if not p or p['invalid'] or not proof or 'PROFILE' not in records:issues.append('result lacks exact policy/proof/profile')
  if any(r[k] not in (0,1) for k in ('active','uncertain','root_allowed','handoff_allowed')):issues.append('invalid live result bool')
  if r['handoff_allowed']:issues.append('live mode can never allow Linux handoff')
  if proof and any(proof[k] not in (0,1) for k in ('verified','rollback','original')):issues.append('invalid proof bool')
  if r['rc']==0:
   classification='verified_x2_retained_root_allowed'
   if r!={'rc':0,'phase':5,'error':0,'active':1,'uncertain':0,'root_allowed':1,'handoff_allowed':0}:issues.append('success lacks exact phase5 retainedx2 proof')
   if proof!={'commands':60,'sets':18,'reads':2,'verified':1,'rollback':0,'original':0}:issues.append('success counts/rollback/original mismatch')
   if ref or 'STOP' in records or 'STATE' in records:issues.append('success contradicts stop/rollback')
  else:
   classification='failed_manual_reset'
   if r['phase']>4 or not r['error'] or r['root_allowed'] or r['active'] or (proof and proof['verified']):issues.append('failure conflicts with root/active/verified')
   if not records.get('STOP') or not records.get('STATE') or not records.get('LAST'):issues.append('failed result missing stop evidence')
   st=records.get('STOP',{});state=records.get('STATE',{})
   if st and(any(st[k]!=r[k] for k in ('rc','phase','error')) or (proof and any(st[k]!=proof[k] for k in ('commands','sets','reads')))):issues.append('failure stop/proof mismatch')
   if state and (state['engine_root'] or state['engine_handoff'] or state['uncertain']!=r['uncertain'] or (proof and any(state[k]!=proof[k] for k in ('rollback','original')))):issues.append('failure state contradicts live outcome')
   if records.get('LAST') and (records['LAST']['root_published'] or records['LAST']['return_blocked']!=1):issues.append('failure claims root publication/return allowed')
   if r['uncertain'] and proof and proof['original']:issues.append('uncertain failure claims original verified')
 else:
  if proof or 'PROFILE' in records:issues.append('proof/profile without live result')
  if p and not ref and 'RUNTIME' not in records:issues.append('armed live outcome not yet observed')
 if 'RUNTIME' in records:
  if not r and set(records)=={'POLICY','RUNTIME'} and p=={'armed':1,'invalid':0,'return_blocked':1}:
   classification='pre_root_io_failure_manual_reset'
  else:
   if classification!='verified_x2_retained_root_allowed':issues.append('I/O panic conflicts with partial/failed/invalid live proof')
   classification='runtime_failure_manual_reset'
 root_lines=[i for i,l in enumerate(text.splitlines(),1) if 'BSD root: ufsroot0' in l or 'exec launchd' in l]
 if root_lines and (not r or r['rc'] or not r['root_allowed']):issues.append('root continuation without verified live proof')
 if root_lines and r:
  resultline=next((x['line'] for x in evidence if x['kind']=='PROOF'),10**9)
  if min(root_lines)<=resultline:issues.append('root publication precedes live proof')
 ranks={'POLICY':0,'RESULT':1,'PROOF':2,'PROFILE':3,'REFUSED':4,'STOP':4,'STATE':5,'LAST':6,'RUNTIME':7}
 if any(ranks[a]>ranks[b] for a,b in zip(order,order[1:])):issues.append('live report order')
 return {'complete':not issues,'classification':classification,'issues':issues,'records':records,'raw_evidence':evidence,'success':not issues and classification=='verified_x2_retained_root_allowed','linux_return_permanently_blocked':bool(p and p['return_blocked']==1),'limits':['LINK/LIMITS are pretransition baseline only','No firmware execution/RF/networking/GUI/speed inferred','Rescue is manual after reset, never automatic Linux fallback']}
def parse(data):
 l=live(data);c=common_ap(data);issues=['live: '+x for x in l['issues']]+['common: '+x for x in c['issues']]
 try:data.decode('utf8')
 except UnicodeDecodeError:issues.append('invalid UTF8 source')
 if not l['records']:issues.append('live result not observed')
 # Wi-Fi common may not run before UFS proof. Its absence in a failed boot is not a successfulskip.
 if l['success'] and c['classification']=='common_not_observed':issues.append('common result not yet observed')
 initial_live_ok=l['complete'] and l['records'].get('RESULT',{}).get('rc')==0 and l['records'].get('RESULT',{}).get('root_allowed')==1
 if c['classification']!='common_not_observed' and not initial_live_ok:issues.append('common continuation without live success')
 return {'complete':not issues,'issues':issues,'live':l,'common':c,'sha256':hashlib.sha256(data).hexdigest(),'observed_combined_success':not issues and l['success'] and c['success']}
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);a=ap.parse_args();r=parse(a.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
