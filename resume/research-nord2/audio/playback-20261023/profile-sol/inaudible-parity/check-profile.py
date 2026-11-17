#!/usr/bin/env python3
import json, math
from pathlib import Path
p=Path(__file__).resolve().parent
x=json.loads((p.parent/'tfa98xx.json').read_text())
assert x['application_type']=='9874'
result=[]
for d,addr,slot,analog in zip(x['devices'],[0x34,0x37],[1,0],[4,5]):
 assert d['address']==addr
 speaker=next(q for q in d['profiles'] if q['name']=='speaker')
 ops=[o for o in d['device_ops']+speaker['ops'] if o['descriptor']==16]
 assert len(ops)==52
 def field(reg,mask,shift):
  relevant=[o for o in ops if o['reg']==reg and o['mask']&mask]
  known=value=0
  for o in relevant:
   known|=o['mask']&mask;value=(value&~o['mask'])|o['value']
  assert known==mask,(reg,mask,known)
  return (value&mask)>>shift
 assert field(0x26,15,0)==slot
 assert field(0x21,0x1f0,4)==31
 assert field(0x22,0x7c,2)==31
 assert field(0x52,0x1fe0,5)==190
 assert field(0x61,0x3c0,6)==analog
 assert field(2,0x10,4)==0
 assert field(0,0x10,4)==1
 assert field(0x62,0xc000,14)==0
 # Board profile does not alter these reset-dependent enables or PWM hard mute.
 for reg,mask in [(0x20,0x10),(0x23,1),(0x58,2)]:
  assert not any(o['reg']==reg and o['mask']&mask for o in ops)
 result.append(dict(address=hex(addr),input_slot=slot,slot_bits=32,sample_bits=32,AMPGAIN_raw=190,TDMSPKG_raw=analog,INPLEV_raw=0,DCA=1,LNMODE=0))
result=dict(profile=result,dma_gain=256/32768,dma_db=20*math.log10(256/32768),test_tone_peak=128,test_tone_dbfs=20*math.log10(128/32768),verdict='PASS',hardware_tested=False)
(p/'profile-evidence.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
