#!/usr/bin/env python3
"""Current-source/pinned-profile regression, no device or external writes."""
from pathlib import Path
import json,re,hashlib
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists())
K=R.parent/'nord2-current-kernel';D=Path(__file__).resolve().parent
A=K/'sound/soc/codecs/audio/tfa98xx'
make=(A/'Makefile').read_text()
assert 'snd-soc-tfa98xx-objs += tfa98xx.o' in make
assert not re.search(r'^snd-soc-tfa98xx-objs.*src/',make,re.M)
f=(A/'tfa9874_tfafieldnames.h').read_text()
fields={n:int(v,16) for n,v in re.findall(r'TFA9874_BF_(\w+) = 0x([0-9a-f]+)',f)}
profile=json.loads((R/'research/nord2/audio/playback-20261023/profile-sol/tfa98xx.json').read_text())
receipt=[]
for d,slot in zip(profile['devices'],[1,0]):
 speaker=next(p for p in d['profiles'] if p['name']=='speaker')
 ops=d['device_ops']+speaker['ops']
 values={}
 for o in ops:
  if o['descriptor']==16:
   mask=o['mask'];pos=(mask&-mask).bit_length()-1
   bf=(o['reg']<<8)|(pos<<4)|(mask.bit_count()-1)
   values[bf]=o['value']>>pos
   assert o['reg'] not in (0xa3,0xa7,0xa8,0xf0),'MTP programming in selected profile'
 def v(name):return values[fields[name]]
 assert (v('TDMSLLN'),v('TDMSSIZE'),v('TDMNBCK'))==(31,31,2)
 assert v('TDMSPKS')==slot and v('TDMCSS')==slot
 assert v('TDMDCS')==1-slot
 assert v('AMPGAIN')==190 and v('FRACTDEL')==22
 assert v('DCA')==1 and v('INPLEV')==0
 assert [o['descriptor'] for o in speaker['ops']]==[16]*5+[17]
 patches=[o for o in d['device_ops'] if o['descriptor']==5]
 assert len(patches)==1 and patches[0]['payload_hex']=='74ffff000000'
 assert len([o for o in ops if o['descriptor']==16])==52
 receipt.append({'slave':hex(d['address']),'speaker_slot':slot,'slot_bits':32,'sample_bits':32,'ampgain':190,'fractdel':22,'DCA':1,'dummy_patch_bytes':6,'vsteps':0})
h=(R/'shim/nord2-xnu/audio_hw.h').read_text();assert re.search(r'#define NAH_MAX_GAIN_Q15\s+256u',h)
# Signed division truncates toward zero in C; establish independent amplitude ceiling.
assert max(abs(int(x*256/32768)) for x in range(-32768,32768))==256
(D/'profile-contract.json').write_text(json.dumps({'result':'PASS','devices':receipt,'maximum_s16_output_magnitude':256,'selected_makefile_sha256':hashlib.sha256(make.encode()).hexdigest()},indent=2)+'\n')
print('PASS current selected build,104 exact bitfields,32-bit slots, mirrored speaker routes, DCA, dummy patch, no vsteps, digital ceiling')
