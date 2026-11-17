#!/usr/bin/env python3
"""VCN33 configuration proof layered on byte-identical strict26/RXRF parser."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
P=Path(__file__).resolve().parent
pins={'core-receipt.py':'6e1bf373970ecd1a4cb7936ac069e47fc51d309546b065aa72fed5a53aaca6c0','core-lifecycle.py':'7384cd597dafb38c5c9c111e68983a75ea7347092cf67406501dff21acac333c'}
for name,digest in pins.items():
 if hashlib.sha256((P/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('receipt dependency identity: '+name)
s=importlib.util.spec_from_file_location('vcn_receipt',P/'core-receipt.py');receipt=importlib.util.module_from_spec(s);s.loader.exec_module(receipt)
LIMIT=receipt.base.LIMIT
SNAP=re.compile(r'NORD2 WIFI VCN33 SNAP: kind=(\d+) phase=(\d+) valid=(0x[0-9a-f]+|0) rc=(0x[0-9a-f]+|0) time_us=(\d+) values=((?:0x[0-9a-f]+|0)(?:/(?:0x[0-9a-f]+|0)){12})')
STATUS=re.compile(r'NORD2 WIFI VCN33: status=(-?\d+) attempted=(\d+) complete=(\d+) mutated=(\d+) writes=(\d+) error=(\d+)')
def parse(data):
 if len(data)>LIMIT:return receipt.parse(data)
 issues=[];clean=[];snap=[];status=[];position=[]
 def ck(ok,message):
  if not ok and len(issues)<64:issues.append(message)
 try:lines=data.decode('utf8').splitlines(keepends=True)
 except UnicodeDecodeError:return receipt.parse(data)
 for index,line in enumerate(lines):
  at=line.find('NORD2 WIFI VCN33');body=line[at:].rstrip('\r\n') if at>=0 else ''
  if at<0:clean.append(line);continue
  try:
   if not line.endswith('\n') or len(body)>511 or line.count('NORD2 WIFI ')!=1:raise ValueError()
   m=SNAP.fullmatch(body)
   if m:
    k,phase,valid,rc,time,words=m.groups();row=dict(kind=int(k),phase=int(phase),valid=int(valid,0),rc=int(rc,0),time_us=int(time),values=[int(x,0) for x in words.split('/')])
    if row['kind']>4 or not 1<=row['phase']<=6 or row['valid']>0x1fff or row['rc']>0xffffffff or row['time_us']>0xffffffffffffffff or any(x>0xffff for x in row['values']):raise ValueError()
    snap.append(row);position.append(('snap',row['kind'],index));continue
   m=STATUS.fullmatch(body)
   if not m:raise ValueError()
   row=dict(zip(('status','attempted','complete','mutated','writes','error'),map(int,m.groups())))
   if not -0x80000000<=row['status']<=0x7fffffff or any(not 0<=row[x]<=0xffffffff for x in row if x!='status'):raise ValueError()
   status.append(row);position.append(('status',0,index))
  except ValueError:ck(False,'malformed/unknown VCN33 row')
 result=receipt.parse(''.join(clean).encode())
 # New stage requires the full successful prior recipe independently.
 fixed={k:[x for x in snap if x['kind']==k] for k in range(4)}
 ck(len(status)==1,'VCN33 status exact count')
 ck(all(len(fixed[k])==1 for k in range(4)),'VCN33 fixed snapshots exact counts')
 periodic=[x for x in snap if x['kind']==4];ck(len(periodic)<=300,'VCN33 periodic bound')
 ck(all(x['valid']==0x1fff for x in snap if x['kind']<4),'VCN33 synchronous snapshot read validity')
 for j,x in enumerate(periodic):
  mask=x['valid'];ck(mask&(mask+1)==0,'VCN33 periodic validity prefix')
  ck(all(x['values'][i]==0 for i in range(13) if not mask&(1<<i)),'VCN33 periodic unobserved placeholders')
  if mask!=0x1fff:ck(j==len(periodic)-1,'VCN33 observer stops after partial periodic read')
 if len(status)==1:ck(status[0]==dict(status=0,attempted=1,complete=1,mutated=1,writes=5,error=0),'VCN33 successful configuration required')
 if all(len(fixed[k])==1 for k in range(4)):
  before,after,precal,ready=[fixed[k][0] for k in range(4)];b=before['values'];a=after['values']
  ck(before['phase']==after['phase']==1 and precal['phase']==2 and ready['phase']==3,'VCN33 fixed phase labels')
  ck(before['rc']&1==1,'VCN33 preflight RC mode')
  ck(not(b[0]&3 or b[1]&3 or b[3]&0x60 or b[4]&0x8000 or b[6]&3 or b[7]&3 or b[9]&0x40 or b[10]&0x8000),'VCN33 cold off/config guards')
  ck(b[2]==b[8]==0x8000 and b[5]&0xf00==0xf00 and b[11]&0xf00==0xf00,'VCN33 initial enable/voltage guards')
  for i in range(13):
   if i in (1,7,12):continue # physical MON and shared VS2 observations are metadata
   wanted=0x8060 if i==2 else 0x8040 if i==8 else (b[i]&~0xf00)|0xd00 if i in (5,11) else b[i]
   ck(a[i]==wanted,'VCN33 exact preserved after readback index '+str(i))
  fixed_order=[(kind,index) for typ,kind,index in position if typ=='snap' and kind<4]
  ck([k for k,_ in fixed_order]==[0,1,2,3],'VCN33 fixed snapshot order')
  first=[x for x in position if x[0]=='status']
  if len(first)==1:ck(fixed_order[1][1]<first[0][2]<fixed_order[2][1],'VCN33 status placement')
  def marker(text):
   found=[i for i,line in enumerate(lines) if text in line]
   return found[0] if len(found)==1 else -1
  rom=marker('NORD2 WIFI ROM CPU ARM:');cal=marker('NORD2 WIFI MAIN ARM: stage=5 action=force-calibration');coex=marker('NORD2 WIFI MAIN ARM: stage=6 action=pccif');n9=marker('NORD2 WIFI MAIN ARM: stage=8 action=n9-finish-and-ready');main=marker('NORD2 WIFI MAIN: status=0 stage=9 ready=1')
  ck(all(x>=0 for x in (rom,cal,coex,n9,main)),'VCN33 source hook marker identity')
  if len(first)==1:ck(first[0][2]<rom,'VCN33 configured before ROM CPU release')
  ck(cal<fixed_order[2][1]<coex,'VCN33 pre-calibration hook placement')
  ck(n9<fixed_order[3][1]<main,'VCN33 post-ready hook placement')
  ck(all(index>fixed_order[3][1] for typ,kind,index in position if typ=='snap' and kind==4),'VCN33 periodic observer after main sync snapshots')
 times=[x['time_us'] for x in snap];ck(times==sorted(times),'VCN33 monotonic captured log time')
 result['issues']=result.get('issues',[])+issues
 valid=bool(not issues and result.get('terminal_receipt_valid'))
 result['complete']=bool(result.get('complete') and valid);result['terminal_receipt_valid']=valid
 result['vcn33_configuration_valid']=valid
 result['vcn33_snapshots']=snap
 result['vcn33_status']=status
 result['vcn33_periodic_observer_partial']=any(x['valid']!=0x1fff for x in periodic)
 result['nominal_vcn33_microvolts']=3300000 if valid else None
 result['physical_mon3_required']=False;result['rf_nonzero_required']=False;result['association_claim']=False
 result['sha256']=hashlib.sha256(data).hexdigest()
 return result
if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('log',type=Path);x=a.parse_args()
 with x.log.open('rb') as f:r=parse(f.read(LIMIT+1))
 print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
