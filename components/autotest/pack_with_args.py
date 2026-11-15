#!/usr/bin/env python3
"""Pack private GPU test image, grafting Mali and its GIC interrupt binding.
No phone operation. System Metal is off until the separate pixel qualification.
"""
from pathlib import Path
import importlib.util,subprocess,sys,json,hashlib,struct,argparse
args=argparse.ArgumentParser(description=__doc__)
args.add_argument("--system-metal",action="store_true",help="Build the separately qualified UI variant; never flash automatically")
args.add_argument("--kernel",type=Path,help="Explicit alternative private kernel; does not replace approved artifacts")
args.add_argument("--output-dir",type=Path,help="Separate artifact directory for a new candidate")
args.add_argument("--image-name",help="Image basename within the output directory")
args.add_argument("--extra-args",default="",help="boot-args appended after nord2_gpu/nord2_metal")
args.add_argument("--audio-package",type=Path,help="N2AP package (tools/nord2-audio-package.py) replacing /chosen nord2-audio-package; needs a kernel whose audio installer upgrades (0282)")
args.add_argument("--drop-arg",action="append",default=[],help="exact token removed from the inherited command line (must occur once)")
args=args.parse_args()
r=Path('$NORD2_WORK');main=Path('$NORD2_REPO');out=args.output_dir.resolve() if args.output_dir else r/('gpu/build/device-ui-candidate' if args.system_metal else 'gpu/build/device-candidate');out.mkdir(exist_ok=True)
if args.image_name:assert Path(args.image_name).name==args.image_name and args.image_name.endswith('.img')
image=out/(args.image_name or ('nord2-ios-metal-ui-stop23.img' if args.system_metal else 'nord2-ios-metal-probe-stop23.img'))
assert not image.exists(), 'refuse to replace an existing boot image'
sys.path.insert(0,str(r/'tools'));import adt
sp=importlib.util.spec_from_file_location('mkadt',r/'tools/mkadt-nord2.py');mod=importlib.util.module_from_spec(sp);sp.loader.exec_module(mod)
source=main/'research/nord2/audio/playback-20261023/ios-calibrated';baseline=(source/'playback.adt').read_bytes();root,end=adt.parse(baseline);assert end==len(baseline)
def walk(n):
 yield n
 for c in n.children:yield from walk(c)
mali=next(n for n in walk(mod.build()) if n.name=='mali');assert not any(n.name=='mali' for n in walk(root))
handles=[struct.unpack('<I',v)[0] for n in walk(root) for k,f,v in n.props if k=='AAPL,phandle'];new=max(handles)+1
arm=next(n for n in root.children if n.name=='arm-io')
gic=next(n for n in arm.children if n.name=='gic')
assert gic.prop('interrupt-controller') is not None
assert len(gic.prop('AAPL,phandle'))==4
original_gic=list(gic.props)
for key,value in [('#interrupt-cells',1),('#address-cells',0)]:
 assert gic.prop(key) in (None,struct.pack('<I',value)), 'incompatible GIC binding'
 if gic.prop(key) is None:gic.props.append((key,0,struct.pack('<I',value)))
# The donor builder's GIC phandle can differ from the installed audio tree.
mali.props=[(k,f,struct.pack('<I',new) if k=='AAPL,phandle' else
             gic.prop('AAPL,phandle') if k=='interrupt-parent' else v)
            for k,f,v in mali.props]
assert mali.prop('interrupt-parent')==gic.prop('AAPL,phandle')
arm.children.append(mali);blob=adt.serialize(root)
arm.children.pop();gic.props=original_gic
assert adt.serialize(root)==baseline, 'unrelated audio/Wi-Fi ADT properties changed'
BIG_MEM=0xec000000
key=b'dram-size'.ljust(32,b'\0')+struct.pack('<I',8)+struct.pack('<Q',0x3e000000)
assert blob.count(key)==1, 'dram-size property not found exactly once'
blob=blob.replace(key,key[:-8]+struct.pack('<Q',BIG_MEM))
if args.audio_package:
 sp2=importlib.util.spec_from_file_location('n2ap',main/'tools/nord2-audio-package.py');n2ap=importlib.util.module_from_spec(sp2);sp2.loader.exec_module(n2ap)
 newpkg=args.audio_package.read_bytes();n2ap.decode(newpkg)
 r2,e2=adt.parse(blob);assert e2==len(blob)
 ch=[c for c in r2.children if c.name=='chosen'];assert len(ch)==1
 old=[i for i,(k,f,v) in enumerate(ch[0].props) if k=='nord2-audio-package'];assert len(old)==1,'nord2-audio-package not found once'
 k,f,v=ch[0].props[old[0]];ch[0].props[old[0]]=(k,f,newpkg)
 blob=adt.serialize(r2)
 print('audio package replaced:',len(v),'->',len(newpkg),'bytes sha256',hashlib.sha256(newpkg).hexdigest())
if 1:  # q10ge: /arm-io/isp (camera-front = camera-rear = 1) so MobileGestalt answers still-camera (camera-userland-20261109/tools/adt_add_isp.py)
 r3,e3=adt.parse(blob);assert e3==len(blob)
 aio=[c for c in r3.children if c.name=='arm-io'];assert len(aio)==1 and not any(c.name=='isp' for c in aio[0].children)
 def _ph(n):
  for k,f,v in n.props:
   if k=='AAPL,phandle' and len(v)==4: yield struct.unpack('<I',v)[0]
  for c in n.children: yield from _ph(c)
 isp=adt.Node();isp.props=[('name',0,b'isp\0'),('device_type',0,b'isp\0'),('AAPL,phandle',0,struct.pack('<I',max(_ph(r3))+1)),('camera-front',0,struct.pack('<I',1)),('camera-rear',0,struct.pack('<I',1))]
 aio[0].children.append(isp);blob=adt.serialize(r3);print('added /arm-io/isp')
(out/'gpu.adt').write_bytes(blob)
ci=json.loads((source/'build-result.json').read_text());base=ci['images'][0]['commandline'].replace(' nord2fbdiag=1','')
for d in args.drop_arg:
 assert (' '+d+' ') in (base+' '),'drop-arg not a unique token: '+d
 assert base.split().count(d)==1,'drop-arg not unique: '+d
 base=(' '+base+' ').replace(' '+d+' ',' ',1).strip()
cmd=base+f' nord2_gpu=1 nord2_metal={int(args.system_metal)}'+((' '+args.extra_args) if args.extra_args else '');assert len(cmd)<608,len(cmd)
toks=cmd.split();assert len(toks)==len(set(t.split('=')[0] for t in toks)),'duplicate boot-arg keys'
kernel=args.kernel.resolve() if args.kernel else r/'gpu/build/kernel-native/kernel.development.nord2-metal';base=main/'research/nord2/rescue/recompressed/base.img'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
kernel_sha=sha(kernel)
assert sha(base)=='ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf'
def run(label,args):
 with (out/(label+'.log')).open('w') as log:subprocess.run([str(a) for a in args],cwd=r,stdout=log,stderr=log,check=True,env={**__import__('os').environ,'NORD2_MEM_SIZE':hex(BIG_MEM)})
run('isa',['python3',r/'tools/nord2-check-rcpc2.py',kernel,'--objdump',main/'../toolchains/proton-clang/bin/llvm-objdump'])
run('strip',[main/'../toolchains/cctools/bin/aarch64-apple-darwin-strip','-S','-x',kernel,'-o',out/'kernel.strip'])
run('payload',['python3',r/'tools/mkpayload.py',out/'kernel.strip',out/'gpu.adt',out/'gpu.payload','--cmdline',cmd])
run('pack',['python3',r/'tools/nord2-pack-xnu.py',base,Path('$NORD2_WORK/gpu/build/bigwin-20261029/loader3776/loader-Image'),out/'gpu.payload',image,'--zopfli',main/'../toolchains/zopfli/zopfli'])
run('check',['python3',r/'tools/nord2-check-xnu.py',image,out/'gpu.payload','--stock',base])
assert 'FAILURES: none' in (out/'check.log').read_text()
assert sha(kernel)==kernel_sha, 'kernel changed during packing'
record={'image':str(image),'sha256':sha(image),'bytes':image.stat().st_size,'boot_spare':33554432-image.stat().st_size,'kernel':kernel_sha,'adt_baseline':sha(source/'playback.adt'),'adt':sha(out/'gpu.adt'),'commandline':cmd,'device_tested':False,'approved':False,'metal_system_registration':args.system_metal}
(out/'image-receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
