#!/usr/bin/env python3
"""Offline strict 20-line X2 preflight evidence decoder. No device access."""
import argparse,base64,hashlib,importlib.util,json,re
from pathlib import Path
PRIOR=Path(__file__).resolve().parent.parent/'ufs-two-lane/hardware-candidate/parse-result.py'
spec=importlib.util.spec_from_file_location('prior_x2',PRIOR)
prior=importlib.util.module_from_spec(spec);spec.loader.exec_module(prior)
ENGINE=['exclusive pre-root ownership','HCE enabled','HCS ready mask 0xb','HCS error mask 0x30 clear','UTRLDBR zero','UTMRLDBR zero','IE zero','AHIT zero','UECPA zero','UECDL zero','UECN zero','UECT zero','UECDME zero','IS zero']
OWN=['diagnostic in progress','diagnostic stop armed','link not uncertain','root controller owned','root not ready','root not writable','not probe-only','LUN equals 2','bdev unpublished','bmajor unpublished','cmajor unpublished','register mapping present','DMA VA matches pinned context','DMA PA matches pinned context','partition first block matches context','partition length matches context','no pending UIC','CPU interrupts enabled','not interrupt context','preemption level zero','UTRLBA matches DMA PA low','UTRLBAU matches DMA PA high','UTRLRSR running']
EXTRA={'PREFLIGHT':['valid','fail','owned_valid','owned_fail']}
for start in range(0,14,4):EXTRA['GATE '+str(start)]=['v'+str(i) for i in range(start,min(start+4,14))]
for start in range(0,23,3):EXTRA['OWN '+str(start)]=['v'+str(i) for i in range(start,min(start+3,23))]
LABELS=list(prior.FIELDS)+list(EXTRA)

def parse(data,allow_line_reset=False):
 engine_names=list(ENGINE)
 if allow_line_reset:engine_names[8]="UECPA zero or exact valid LINERESET 0x80000010"
 policy={"name":"line-reset-exact-v1" if allow_line_reset else "original-8702715-zero-only", "allow_line_reset":bool(allow_line_reset),"uecpa_accepted_values":[0,0x80000010] if allow_line_reset else [0],"selection":"explicit caller/CLI choice; never inferred from register data", "source":"selected Android ufshcd.c6113-6126 / ufshci.h212-214" if allow_line_reset else "CI39503015652/8702715 engine healthy exact-zero predicate"}
 base=prior.parse(data);issues=list(base['issues']);rows={};order=[];evidence=[]
 text=data.decode('utf-8',errors='replace')
 try:data.decode('utf-8',errors='strict')
 except UnicodeDecodeError:issues.append('invalid UTF-8 capture')
 for line_no,line in enumerate(text.splitlines(keepends=True),1):
  if 'NORD2 UFS X2 ' not in line:continue
  if line.count('NORD2 UFS X2 ')!=1:issues.append('interleaved X2 markers')
  payload=line.split('NORD2 UFS X2 ',1)[1]
  if ':' not in payload:issues.append('malformed X2 label');continue
  label,body=payload.split(':',1);order.append(label)
  if label not in LABELS:issues.append('unknown X2 label '+label);continue
  if label not in EXTRA:continue
  evidence.append({'line':line_no,'raw':line,'kind':label})
  if label in rows:issues.append('duplicate '+label)
  if not line.endswith('\n'):issues.append('unterminated '+label)
  if len(('NORD2 UFS X2 '+payload).rstrip('\r\n').encode())>=255:issues.append('possible truncation '+label)
  pattern=r'\s+'+r'\s+'.join(re.escape(k)+r'=(0x[0-9a-fA-F]+|[0-9]+)' for k in EXTRA[label])+r'\r?\n'
  match=re.fullmatch(pattern,body)
  if not match:issues.append('malformed fields '+label);continue
  values=[int(v,16 if v.startswith('0x') else 10) for v in match.groups()]
  limit=(1<<64)-1 if label.startswith('OWN ') else (1<<32)-1
  if any(v>limit for v in values):issues.append('integer overflow '+label)
  rows[label]=dict(zip(EXTRA[label],values))
 if order!=LABELS:issues.append('expected exactly20 ordered report lines')
 decoded={};failure=None
 if len(rows)==len(EXTRA) and not issues:
  p=rows['PREFLIGHT']
  ev=[rows['GATE '+str(i//4*4)]['v'+str(i)] for i in range(14)]
  ov=[rows['OWN '+str(i//3*3)]['v'+str(i)] for i in range(23)]
  def check(name,mask,fail,vals,names,predicate):
   nonlocal failure
   full=(1<<len(vals))-1
   if mask>full or mask&(mask+1):issues.append(name+' noncontiguous/invalid validity mask')
   count=mask.bit_count()
   if fail>len(vals) or (fail and (fail!=count or not count)):issues.append(name+' first failure/mask mismatch')
   if not fail and mask not in (0,full):issues.append(name+' partial mask without failure')
   decoded[name]=[{'index':i,'predicate':names[i],'state':'observed' if mask>>i&1 else 'UNREAD','value':v if mask>>i&1 else None} for i,v in enumerate(vals)]
   for i,v in enumerate(vals):
    if not(mask>>i&1):
     if v:issues.append(name+' nonzero unobserved storage')
     continue
    passed=predicate(i,v)
    if passed is not None and passed!=(fail!=i+1):issues.append(name+' predicate value contradicts failure '+str(i))
  def ep(i,v):return bool(v) if i==0 else bool(v&1) if i==1 else (v&0xb)==0xb if i==2 else not(v&0x30) if i==3 else (v==0 or (allow_line_reset and i==8 and v==0x80000010))
  def op(i,v):
   if i in (12,13,14,15):return None # expected pinned context is not printed
   if i in (0,1,3,11,17):return v!=0
   if i==7:return v==2
   if i==20:return v==(ov[13]&0xffffffff)
   if i==21:return v==(ov[13]>>32)
   if i==22:return bool(v&1)
   return v==0
  check('engine',p['valid'],p['fail'],ev,engine_names,ep)
  check('ownership',p['owned_valid'],p['owned_fail'],ov,OWN,op)
  stop=base['records']['STOP']
  if p['fail']:
   if stop['error']!=-2 or stop['phase']!=1 or any(stop[k] for k in ('commands','sets','reads')):issues.append('initial refusal contradicts engine counters')
   if p['fail']==1:
    if not p['owned_fail']:issues.append('ownership refusal missing owned failure')
   elif p['owned_fail'] or p['owned_valid']!=0x7fffff:issues.append('later engine failure without completed ownership')
   failure={'engine_index':p['fail']-1,'predicate':engine_names[p['fail']-1]} if p['fail']<=14 else None
   if failure and p['fail']==1 and 1<=p['owned_fail']<=23:failure['ownership_predicate']=OWN[p['owned_fail']-1]
  elif p['valid']:
   if p['owned_fail'] or p['owned_valid']!=0x7fffff:issues.append('passed initial gate without passed ownership')
   if stop['error']==-2:issues.append('initial error without failed predicate')
  elif p['owned_valid'] or p['owned_fail'] or stop['phase']!=0 or stop['rc']==0:issues.append('absent preflight inconsistent with engine entry')
 return {'source_policy':policy,'complete':not issues,'success':not issues and base['success'], 'classification':base['classification'] if not issues else 'incomplete_or_corrupt','issues':issues,'failure':failure,'observations':decoded,'preflight_records':rows,'prior_report':base,'raw_text':text,'raw_base64':base64.b64encode(data).decode('ascii'),'source_sha256':hashlib.sha256(data).hexdigest(),'source_bytes':len(data),'raw_evidence':evidence,'limits':['Preflight data alone never establishes link success.','Only full prior diagnostic report can report validated x2 and x1 rollback; no performance claim.','Ownership equality indices12..15 cannot be independently checked: expected context is not printed.','UNREAD is distinct from an observed zero.']}
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);ap.add_argument('--allow-line-reset',action='store_true',help='Explicit future-image policy: accept only UECPA0 or exact0x80000010; historical images default zero-only');args=ap.parse_args();r=parse(args.log.read_bytes(),allow_line_reset=args.allow_line_reset);print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
