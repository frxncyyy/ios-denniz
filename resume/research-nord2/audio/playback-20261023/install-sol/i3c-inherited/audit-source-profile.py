#!/usr/bin/env python3
"""Bounded read-only selected-source/profile consistency checks; no hardware."""
from pathlib import Path
import re,json,struct,hashlib
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists())
K=R.parent/'nord2-current-kernel';S=R/'shim/nord2-xnu';B=Path(__file__).resolve().parent
src=(K/'sound/soc/codecs/audio/tfa98xx/tfa_init.c').read_text()
ours=(S/'audio_amp.c').read_text()
checks={}
for rev,name in [('0a73','ra73'),('0b73','rb73'),('0a74','ra74'),('0b74','rb74'),('0c74','rc74')]:
 case=re.search(r'case 0x'+rev+r'\s*:[\s\S]*?break;',src).group()
 expected=[(int(a,16),int(b,16))for a,b in re.findall(r'tfa_reg_write\(tfa,\s*0x([0-9a-fA-F]+),\s*0x([0-9a-fA-F]+)\)',case)]
 table=re.search(r'\b'+name+r'\[\]=\{(.*?)\};',ours).group(1)
 actual=[(int(a,16),int(b,16))for a,b in re.findall(r'\{0x([0-9a-fA-F]+),0x([0-9a-fA-F]+)\}',table)]
 assert actual==expected,(rev,actual,expected)
 checks['errata_'+rev]={'ops':len(actual),'exact_selected_source_match':True}
P=R/'research/nord2/audio/playback-20261023/profile-sol'
blob=(P/'tfa98xx.cnt').read_bytes();assert hashlib.sha256(blob).hexdigest()=='635300fbc96f258ff9a35f82435f2e9d3ff6f1370c156067b40146e011038692'
decoded=json.loads((P/'tfa98xx.json').read_text())
rows=re.findall(r'\{([0-9,]+)\}',(S/'audio_board_offsets.h').read_text());assert len(rows)==2
for amp,(device,row) in enumerate(zip(decoded['devices'],rows)):
 offsets=list(map(int,row.split(',')));assert len(offsets)==52
 speaker=next(p for p in device['profiles']if p['name']=='speaker')
 operations={o['offset']:o for o in device['device_ops']+speaker['ops']if o['descriptor']==16}
 assert offsets==[o['offset']for o in device['device_ops']+speaker['ops']if o['descriptor']==16]
 for offset in offsets:
  value,field=struct.unpack_from('<HH',blob,offset);reg=field>>8;pos=(field>>4)&15;width=(field&15)+1
  mask=((1<<width)-1)<<pos;desired=value<<pos;o=operations[offset]
  assert (reg,mask,desired)==(o['reg'],o['mask'],o['value'])
  assert reg not in (0xf,0xa0,0xa1,0xa3,0xa7,0xa8)
  if reg==0:assert not(mask&9),'profile touches PWDN/AMPE'
  if reg==2:assert not(mask&15),'profile overwrites final AUDFS48k'
 checks['profile_amp'+str(amp)]={'address':device['address'],'ops':len(offsets),'exact_blob_and_decode_match':True,'preserves_pwdn_ampe_and_audfs':True}
files=[K/'drivers/i2c/busses/i2c-mtk.c',K/'drivers/i2c/busses/i2c-mtk.h',K/'arch/arm64/boot/dts/mediatek/mt6893.dts',K/'sound/soc/codecs/audio/tfa98xx/tfa_init.c',K/'sound/soc/codecs/audio/tfa98xx/tfa_dsp.c',S/'audio_i2c8.c',S/'audio_amp.c',S/'audio_hw.c',S/'audio_board.c',S/'audio_board_offsets.h']
report={'result':'PASS source/profile consistency; physical transactions untested','checks':checks,'files':{str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in files}}
(B/'source-profile-receipt.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS five exact errata tables +104 selected board bitfields, power/output/rate preservation; no device action')
