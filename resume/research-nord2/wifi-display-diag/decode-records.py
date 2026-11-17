#!/usr/bin/env python3
"""Offline strict decoder of bounded NORD2 FB RECORD dumps. No device access."""
import argparse,json,re,struct
from pathlib import Path
SIZE=0x46c
MARK='NORD2 FB RECORD:'
ROW=re.compile(r'NORD2 FB RECORD: accepted=([12]) offset=(0|0x[0-9a-f]+) words=([1-8]) data=([0-9a-f]{8}(?:,[0-9a-f]{8}){7})$')
def decode(data):
 groups={1:{},2:{}};errors=[]
 for line,raw in enumerate(data.decode('utf-8',errors='replace').splitlines(keepends=True),1):
  if MARK not in raw:continue
  segment=raw[raw.index(MARK):].rstrip('\r\n');m=ROW.fullmatch(segment)
  if not raw.endswith(('\n','\r')) or not m:
   errors.append({'line':line,'error':'truncated or malformed record line','raw':segment});continue
  accepted=int(m[1]);off=int(m[2],0);count=int(m[3]);words=[int(v,16) for v in m[4].split(',')]
  expected=min(8,(SIZE-off)//4) if 0<=off<SIZE else 0
  if off%32 or not expected or count!=expected or any(words[count:]):
   errors.append({'line':line,'accepted':accepted,'error':'invalid offset/count/padding','raw':segment});continue
  if off in groups[accepted]:
   errors.append({'line':line,'accepted':accepted,'error':'duplicate/overlapping offset','offset':off});continue
  groups[accepted][off]=words[:count]
 records=[]
 for accepted,parts in groups.items():
  missing=[off for off in range(0,SIZE,32) if off not in parts]
  # Any malformed row invalidates the capture rather than silently accepting
  # a complete duplicate generation or an unassignable bad accepted ID.
  valid=not missing and not errors
  record={'accepted':accepted,'complete':valid,'groups':len(parts),'missing_offsets':missing}
  if valid:
   payload=b''.join(struct.pack('<'+'I'*len(parts[o]),*parts[o]) for o in range(0,SIZE,32))
   assert len(payload)==SIZE
   get=lambda o:struct.unpack_from('<I',payload,o)[0]
   record.update(bytes=SIZE,hex=payload.hex(),nonzero_words=[{'offset':o,'value':get(o)} for o in range(0,SIZE,4) if get(o)],current_interpretation={'label':'Existing kernel ABI interpretation only; not verified layout or GUI evidence','seq':get(0x50),'mask':get(0x104),'ids':[get(0x54+4*i) for i in range(4)]})
  records.append(record)
 return {'errors':errors,'records':records,'gui_ready':None}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();print(json.dumps(decode(a.log.read_bytes()),indent=2))
