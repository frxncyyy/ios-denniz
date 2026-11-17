#!/usr/bin/env python3
"""Bounded TFA24BE multi-message inventory, not a DSP parameter decoder.
Protocol source: selected root tfa_container.c:tfa_tib_dsp_msgmulti.
No units or active/runtime status inferred from command names/payload values.
"""
from pathlib import Path
import hashlib,json
NAMES={(0,6):'FW_SET_INPUT_SELECTOR',(0,8):'FW_SET_OUTPUT_SELECTOR',(0,9):'FW_SET_PROGRAM_CONFIG',(0,10):'FW_SET_GAINS',(0,4):'FW_SET_SENSES_DELAY',(0,5):'FW_SET_SENSESCAL',(1,1):'SB_SET_LAGW',(1,6):'SB_SET_LSMODEL',(1,10):'SB_SET_EXCURSION_FILTERS',(1,0):'SB_SET_ALGO_PARAMS',(1,7):'SB_SET_MBDRC'}
def parse(b):
 if len(b)>4000 or len(b)<6 or len(b)%3 or b[:3]!=bytes.fromhex('008015'):raise ValueError('size/header')
 o=3;packets=[]
 while o<len(b):
  lo=o;n=int.from_bytes(b[o:o+3],'big');o+=3
  if n==0:
   if o!=len(b):raise ValueError('trailing bytes')
   return packets
  if n<1 or n>(len(b)-o)//3:raise ValueError('word length')
  c=b[o:o+3];module=c[1]&0x7f
  entry=dict(length_offset=lo,command_offset=o,length_words_including_command=n,payload_offset=o+3,payload_words=n-1,command_hex=c.hex(),module=module,parameter_id=c[2],name=NAMES.get((module,c[2]),'opaque/undocumented command'))
  if (module,c[2])==(0,10):entry['raw_payload_words_no_units']=[int.from_bytes(b[j:j+3],'big')for j in range(o+3,o+n*3,3)]
  packets.append(entry);o+=n*3
 raise ValueError('missing terminal zero')
def main():
 root=Path(__file__).resolve().parent;result=[]
 for p in sorted((root/'extracted/etc/smartpa_param').glob('*.bin')):
  b=p.read_bytes();packets=parse(b)
  for i in range(len(b)):
   try:parse(b[:i])
   except ValueError:pass
   else:raise AssertionError(('accepted truncation',p,i))
  corrupt=bytearray(b);corrupt[3:6]=b'\xff\xff\xff'
  try:parse(corrupt)
  except ValueError:pass
  else:raise AssertionError('accepted unbounded length')
  result.append(dict(path=str(p.relative_to(root)),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),packets=packets))
 assert len(result)==4
 (root/'dsp-packet-receipt.json').write_text(json.dumps(dict(protocol='24bit big-endian008015/word-length/messages/terminal0',source='root tfa_container.c2594-2698; command IDs root tfa_dsp_fw.h50-107',test='all truncations and corrupt length rejected',hardware_tested=False,runtime_usage_established=False,files=result),indent=2)+'\n')
 print('PASS4 exact packet inventories; all truncations/corrupt lengths rejected; parameterunits opaque')
if __name__=='__main__':main()
