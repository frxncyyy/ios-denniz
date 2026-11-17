import ctypes as C,hashlib,json,subprocess,tempfile,pathlib
root=pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td)
 (p/'test.c').write_text('#include "audio_board.c"\nint na8_read16(const struct na8_io*a,void*b,struct na8_state*c,unsigned d,unsigned e,uint16_t*f){return -1;}\nint na8_write16(const struct na8_io*a,void*b,struct na8_state*c,unsigned d,unsigned e,uint16_t f){return -1;}\nint test_supports(unsigned rev,unsigned addr){return supports(rev,addr); }\nunsigned test_op(unsigned a,unsigned i,unsigned f){return f==0?plan[a][i].reg:f==1?plan[a][i].mask:plan[a][i].value;}\n')
 subprocess.run(['cc','-shared','-fPIC','-DNORD2','-I'+str(root),'-I'+str(root.parent/'hardware-sol'),str(p/'test.c'),'-o',str(p/'test.so')],check=True)
 lib=C.CDLL(str(p/'test.so'));lib.nah_board_profile.restype=C.c_void_p
 raw=(root/'tfa98xx.cnt').read_bytes();digest=hashlib.sha256(raw).digest()
 assert not lib.nah_board_profile()
 # Reject truncations and digest corruption before a good one-time load.
 for n in range(len(raw)): assert lib.nah_board_load(raw,n,digest)==-1
 assert lib.nah_board_load(raw,len(raw),bytes(32))==-1
 assert lib.nah_board_load(raw,len(raw),digest)==0
 assert lib.nah_board_profile()
 for r in (0x0a74,0x0b74,0x0c74):
  for a in (0x34,0x37): assert lib.test_supports(r,a)==1
 for r,a in ((0x0a73,0x34),(0x0d74,0x37),(0x0a74,0x35)):assert lib.test_supports(r,a)==0
 assert lib.nah_board_load(raw,len(raw),digest)==-1
 expected=json.loads((root/'tfa98xx.json').read_text())
 for a,d in enumerate(expected['devices']):
  ops=[o for o in d['device_ops']+d['profiles'][0]['ops'] if 'reg'in o and o['reg']not in(15,160,161)]
  assert len(ops)==52
  for i,o in enumerate(ops):
   assert [lib.test_op(a,i,f) for f in range(3)]==[o['reg'],o['mask'],o['value']],(a,i)
 print('PASS:104 decodedoperations equalPython;alltruncations/baddigest/reloadreject')
