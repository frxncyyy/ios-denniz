from pathlib import Path
import hashlib,importlib.util,json
P=Path(__file__).resolve().parent;R=P.parents[3]
s=importlib.util.spec_from_file_location('vcnparser',P/'parse-backend.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
fixture=R/'research/nord2/wifi-rx-lifecycle/ios/backend-first-receipt.log'
b=fixture.read_bytes();assert len(b)==204038 and hashlib.sha256(b).hexdigest()=='465dabc7732177ad311a04b28dc3afdeaec5f4a413715c6187537967daf46dff'
assert m.receipt.parse(b)['complete']
before=[0,0,0x8000,0,0,0xf00,0,0,0x8000,0,0,0xf00,0x35]
after=before.copy();after[2]=0x8060;after[8]=0x8040;after[5]=after[11]=0xd00
def snap(k,phase,t,values=None,valid=0x1fff,rc=0x401425):
 return 'NORD2 WIFI VCN33 SNAP: kind=%u phase=%u valid=%#x rc=%#x time_us=%u values=%s\n'%(k,phase,valid,rc,t,'/'.join('%#x'%x for x in (values if values is not None else after)))
status='NORD2 WIFI VCN33: status=0 attempted=1 complete=1 mutated=1 writes=5 error=0\n'
rows=[snap(0,1,1,before),snap(1,1,2),snap(2,2,3),snap(3,3,4)]
def compose(r=rows,st=status,periodic=''):
 text=b.decode();text=text.replace('NORD2 WIFI ROM CPU ARM:',r[0]+r[1]+st+'NORD2 WIFI ROM CPU ARM:',1)
 lines=text.splitlines(keepends=True);i=next(i for i,l in enumerate(lines) if 'MAIN ARM: stage=5 action=force-calibration' in l);lines.insert(i+1,r[2]);text=''.join(lines).replace('NORD2 WIFI MAIN: status=0 stage=9 ready=1',r[3]+'NORD2 WIFI MAIN: status=0 stage=9 ready=1',1)
 return (text+periodic).encode()
positive=negative=0
def yes(data):
 global positive
 result=m.parse(data);assert result['complete'] and result['vcn33_configuration_valid'],result['issues'];assert not result['physical_mon3_required'] and not result['association_claim'];positive+=1
 return result
def no(data):
 global negative
 result=m.parse(data);assert not result['complete'],result;negative+=1
yes(compose());yes(compose(periodic=snap(4,5,5)));yes(compose(periodic=snap(4,6,5)))
for mon in [0,1,2,3,0xffff]:
 a=after.copy();a[1]=a[7]=mon;a[12]=0xffff
 r=rows.copy();r[1]=snap(1,1,2,a);r[2]=snap(2,2,3,a);r[3]=snap(3,3,4,a);yes(compose(r))
for bits in range(14):
 mask=(1<<bits)-1;v=after.copy()
 for i in range(13):
  if not mask&(1<<i):v[i]=0
 result=yes(compose(periodic=snap(4,4,5,v,mask)));assert result['vcn33_periodic_observer_partial']==(mask!=0x1fff)
no(b)
no(compose(st=""));no(compose(st=status+status))
no(compose(periodic="".join(snap(4,5,5+i) for i in range(301))))
no(compose(periodic=snap(4,5,3)))
for k in range(4):
 r=rows.copy();r[k]='';no(compose(r));r=rows.copy();r[k]+=r[k];no(compose(r))
for field,wrong in [('status','-1'),('attempted','0'),('complete','0'),('mutated','0'),('writes','4'),('writes','6'),('error','1')]:
 import re
 st=re.sub(r'\b'+field+r'=-?\d+',field+'='+wrong,status);no(compose(st=st))
for i in range(13):
 if i in [1,7,12]:continue
 a=after.copy();a[i]^=1;r=rows.copy();r[1]=snap(1,1,2,a);no(compose(r))
for v in [0x2,0x100,0x2000]:
 no(compose(periodic=snap(4,5,5,[0]*13,v)))
no(compose(periodic=snap(4,5,5,[0]*13,0)+snap(4,5,6)))
a=[0]*13;a[-1]=1;no(compose(periodic=snap(4,5,5,a,0)))
for replacement in ['kind=5','phase=7','phase=0','valid=0x2000','rc=0x100000000','time_us=18446744073709551616']:
 key=replacement.split('=')[0];r=rows.copy();import re;r[0]=re.sub(key+r'=(?:0x[0-9a-f]+|\d+)',replacement,r[0],count=1);no(compose(r))
a=before.copy();a[0]=0x10000;r=rows.copy();r[0]=snap(0,1,1,a);no(compose(r))
for i in [0,1,3,4,6,7,9,10]:
 a=before.copy();a[i]|=0x60 if i in(3,9) else 0x8000 if i in(4,10) else 1;r=rows.copy();r[0]=snap(0,1,1,a);no(compose(r))
for text in ['NORD2 WIFI VCN33 UNKNOWN: value=0\n',rows[0][:-1],rows[0].replace(' values=',' spare=0 values='),rows[0].replace('values=','values=0/'),rows[0].replace('values=0/','values=-1/',1)]:no(compose()+text.encode())
r=rows.copy();r[0],r[1]=r[1],r[0];no(compose(r));r=rows.copy();r[3]=r[3].replace('time_us=4','time_us=0');no(compose(r))
# Source-hook order matters even though the unchanged old recipe still succeeds.
x=compose();line=rows[2].encode();x=x.replace(line,b'')+line;no(x)
print(json.dumps({'positive':positive,'negative':negative,'fixture_kind':'synthetic VCN33 rows around immutable actual prior receipt; no VCN33 hardware claim','PASS':True}))
