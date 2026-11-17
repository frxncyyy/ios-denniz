#!/usr/bin/env python3
"""Host-only actual IOUC wire/lifetime regression; no device or firmware IO."""
from pathlib import Path
import subprocess,tempfile,argparse
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());B=R/'tools/fixtures/nord2-wifi-ios-client';S=R/'shim/nord2-xnu'
import hashlib,json,re
hashfile=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
inputs=[S/'wifi_ios_client.cpp',B/'test-xnu.h',P/'test.cpp',Path(__file__).resolve()]
inputs += list(S.glob('*.h'))+[S/('wifi_ios_'+x+'.c') for x in ('binding','control','event','lifecycle','card','policy','rates','mapping','age','snapshot','bss','scan_contract','cursor','scalar')]+[S/'wifi_scan_bss.c']
before={str(x.relative_to(R)):hashfile(x) for x in inputs}
ap=argparse.ArgumentParser();ap.add_argument('--log',type=Path);ap.add_argument('--candidate',type=Path,help='Optional reviewed private source overlay; default tests canonical runtime');args=ap.parse_args()
names=('binding','control','event','lifecycle','card','policy','rates','mapping','age','snapshot','bss','scan_contract','cursor','scalar')
with tempfile.TemporaryDirectory(prefix='niwl-compat-') as t:
 d=Path(t);dev=d/'xnu/bsd/dev';arm=d/'xnu/iokit/Kernel/arm';dev.mkdir(parents=True);arm.mkdir(parents=True)
 for f in S.iterdir():
  if f.suffix in ('.h','.c','.cpp'):((arm if f.suffix=='.cpp' else dev)/f.name).write_bytes(f.read_bytes())
 if args.candidate:
  for f in args.candidate.iterdir():((arm if f.suffix=='.cpp' else dev)/f.name).write_bytes(f.read_bytes())
 h=(B/'test-xnu.h').read_text().replace('kIODirectionOut=2','kIODirectionOut=2,kIODirectionIn=1').replace('virtual size_t readBytes(size_t,void*,size_t)=0;','virtual size_t readBytes(size_t,void*,size_t)=0;virtual size_t writeBytes(size_t,const void*,size_t)=0;')
 h=h.replace('bool failPrepare=false,failRead=false,failComplete=false;','bool failPrepare=false,failRead=false,failWrite=false,failComplete=false;unsigned writes=0;')
 h=h.replace('assert(dir==kIODirectionOut);','assert(dir==kIODirectionOut||dir==kIODirectionIn);').replace('assert(dir==kIODirectionOut&&depth);','assert((dir==kIODirectionOut||dir==kIODirectionIn)&&depth);')
 h=h.replace('std::memcpy(p,(char*)bytes+o,n);return n;}};', 'std::memcpy(p,(char*)bytes+o,n);return n;}size_t writeBytes(size_t o,const void*p,size_t n)override{assert(depth);writes++;if(failWrite)return n?n-1:0;if(o>length||n>length-o)return 0;std::memcpy((char*)bytes+o,p,n);return n;}};')
 h=h.replace('size_t structureOutputSize=0;', 'size_t structureOutputSize=0,structureOutputDescriptorSize=0;');(d/'test-xnu.h').write_text(h)
 (d/'test.cpp').write_bytes((P/'test.cpp').read_bytes())
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),'-I'+str(dev),'-I'+str(arm)]
 objs=[]
 for name in names+('scan_bss',):
  filename='wifi_scan_bss.c' if name=='scan_bss' else 'wifi_ios_'+name+'.c';f=dev/filename;o=d/(name+'.o');subprocess.run(['clang','-std=c11',*flags,'-c',str(f),'-o',str(o)],check=True);objs.append(str(o))
 subprocess.run(['clang++','-std=c++17',*flags,'-pthread',str(d/'test.cpp'),*objs,'-o',str(d/'test')],check=True)
 result=subprocess.run([str(d/'test')],text=True,capture_output=True);(P/'run.log').write_text(result.stdout+result.stderr);print(result.stdout);print(result.stderr);result.check_returncode()
 assert re.search(r'COMPLETE: .*cache_status=0 rows=3 omitted=4 omit_mask=0xf',result.stdout), 'actual emitter omission accounting mismatch'
 assert before=={str(x.relative_to(R)):hashfile(x) for x in inputs},'source changed during composition test'
 (P/'verification.json').write_text(json.dumps({'status':'PASS_SYNTHETIC_ONLY','sources':before,'actual_client_unmodified':True,'seams':['IOKit/task/memory/async host doubles from existing canonical fixture','native scan_info/enqueue/view/release callbacks mocked; source22 channel list','synthetic RXD+beacons through actual canonical decoder','no hardware/radio/Settings claim'],'asserted':['explicit13 10flags + LOW_LATENCY32/RSSI INT32_MIN envelope','exact queued13 subset','7 actual decoded records =>3 rows/4 omission categories','actual52B event10 with status0','GET11 valid rows1/6/13 then repeated exactEOF','retained references/mappings/tasks released']},indent=2)+'\n')
