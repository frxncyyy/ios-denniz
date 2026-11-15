#!/usr/bin/env python3
"""0790 host test runner (derived from tools/test-nord2-wifi-ios-client.py --only card). usage: run-rssi-test.py CANDIDATE_DIR [--no-0790]
CANDIDATE_DIR = mk-cand.py output (stage sources + wifi_rssi_bounds.c). --no-0790: baseline (CI names, no wrapper) for GET106 = unsupported."""
from pathlib import Path
import subprocess,tempfile,sys
H=Path(__file__).resolve().parent
R=next(x for x in H.parents if (x/'tools/fixtures/nord2-wifi-ios-client').exists() and (x/'shim/nord2-xnu').exists())
P=R/'tools/fixtures/nord2-wifi-ios-client';S=R/'shim/nord2-xnu';C=Path(sys.argv[1]);base='--no-0790' in sys.argv
names=('join','station_view','binding','control','event','lifecycle','card','policy','rates','mapping','age','snapshot','bss','scan_contract','cursor','scalar')
with tempfile.TemporaryDirectory(prefix='n2rb-') as t:
 d=Path(t);dev=d/'xnu/bsd/dev';arm=d/'xnu/iokit/Kernel/arm';dev.mkdir(parents=True);arm.mkdir(parents=True)
 for src in (S,C):
  for f in src.iterdir():
   if f.suffix in ('.h','.c','.cpp','.inc'):((arm if f.suffix=='.cpp' else dev)/f.name).write_bytes(f.read_bytes())
 inv=(C/'wifi_station_service_bind.inc').read_text();a=inv.index('int nord2_wifi_join_card_inventory(');b=inv.index('\n}',a)+2
 (d/'test-card-inventory.inc').write_text(inv[a:b]+'\n')
 h=(P/'test-xnu.h').read_text().replace('kIODirectionOut=2','kIODirectionOut=2,kIODirectionIn=1').replace('virtual size_t readBytes(size_t,void*,size_t)=0;','virtual size_t readBytes(size_t,void*,size_t)=0;virtual size_t writeBytes(size_t,const void*,size_t)=0;')
 h=h.replace('bool failPrepare=false,failRead=false,failComplete=false;','bool failPrepare=false,failRead=false,failWrite=false,failComplete=false;unsigned writes=0;')
 h=h.replace('assert(dir==kIODirectionOut);','assert(dir==kIODirectionOut||dir==kIODirectionIn);').replace('assert(dir==kIODirectionOut&&depth);','assert((dir==kIODirectionOut||dir==kIODirectionIn)&&depth);')
 h=h.replace('std::memcpy(p,(char*)bytes+o,n);return n;}};', 'std::memcpy(p,(char*)bytes+o,n);return n;}size_t writeBytes(size_t o,const void*p,size_t n)override{assert(depth);writes++;if(failWrite)return n?n-1:0;if(o>length||n>length-o)return 0;std::memcpy((char*)bytes+o,p,n);return n;}};')
 h=h.replace('size_t structureOutputSize=0;', 'size_t structureOutputSize=0,structureOutputDescriptorSize=0;');(d/'test-xnu.h').write_text(h)
 src=(P/'test-integrated.cpp').read_text().replace('"LifecycleShell.cpp"','"wifi_ios_client.cpp"').replace('int main(){','static int oldmain(){').replace('out->count=1;','out->scanning=active;out->count=1;')
 ix=src.rfind('}');src=src[:ix]+' return 0;\n'+src[ix:]
 src+=(P/'test-join-stubs.inc').read_text()
 test=(H/'test-rssi.inc').read_text()
 if base:test='extern "C" {int n2rb_threshold=-80;}\n'+test   # baseline: no wrapper object
 (d/'rssi.cpp').write_text(src+test)
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),'-I'+str(dev),'-I'+str(arm)]
 objs=[]
 for n in names:
  extra=[] if base else (['-Dniw_decode=niv_decode'] if n=='control' else ['-Dniwj_state_reply=niwk_state_reply'] if n=='station_view' else [])
  o=d/(n+'.o');subprocess.run(['clang','-std=c11',*flags,*extra,'-c',str(dev/('wifi_ios_'+n+'.c')),'-o',str(o)],check=True);objs.append(str(o))
 if not base:
  o=d/'rssi_bounds.o';subprocess.run(['clang','-std=c11',*flags,'-c',str(dev/'wifi_rssi_bounds.c'),'-o',str(o)],check=True);objs.append(str(o))
 subprocess.run(['clang++','-std=c++17',*flags,'-Wno-unused-function','-pthread',str(d/'rssi.cpp'),*objs,'-o',str(d/'rssi')],check=True)
 r=subprocess.run([str(d/'rssi')],text=True,capture_output=True)
 out=r.stdout+r.stderr
 print('\n'.join(l for l in out.split('\n') if 'QUERY' not in l))
 sys.exit(r.returncode)
