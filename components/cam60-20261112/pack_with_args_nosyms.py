#!/usr/bin/env python3
"""(cam60-20261112: copy of device-mmu-q10hq-candidate/pack_with_args.py with the kernel fully stripped.) Pack private GPU test image, grafting Mali and its GIC interrupt binding.
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
# ram5088-20261113: NORD2_PHYS_BASE/NORD2_MEM_SIZE pick the window (unset = 3776 MiB at 0x1_42000000 with loader3776; 0x100000000/0x13e000000 =
# 5088 MiB with the ram5g-20261103 loader, as device-mmu-p83-candidate/pack_with_args.py). The kernel must come from build_p1_candidate.py run
# with the same env (Wi-Fi layout object patches).
import os as _os
BIG_BASE=int(_os.environ.get('NORD2_PHYS_BASE','0x142000000'),0);BIG_MEM=int(_os.environ.get('NORD2_MEM_SIZE','0xec000000'),0)
LOADERS={(0x142000000,0xec000000):'bigwin-20261029/loader3776/loader-Image',(0x100000000,0x13e000000):'ram5g-20261103/loader/loader-Image'}
assert (BIG_BASE,BIG_MEM) in LOADERS,('unsupported window',hex(BIG_BASE),hex(BIG_MEM))
LOADER=Path('$NORD2_WORK/gpu/build')/LOADERS[(BIG_BASE,BIG_MEM)]
key=b'dram-size'.ljust(32,b'\0')+struct.pack('<I',8)+struct.pack('<Q',0x3e000000)
assert blob.count(key)==1, 'dram-size property not found exactly once'
blob=blob.replace(key,key[:-8]+struct.pack('<Q',BIG_MEM))
key=b'dram-base'.ljust(32,b'\0')+struct.pack('<I',8)+struct.pack('<Q',0x142000000)
assert blob.count(key)==1, 'dram-base property not found exactly once'
blob=blob.replace(key,key[:-8]+struct.pack('<Q',BIG_BASE))
print('window: dram-base',hex(BIG_BASE),'dram-size',hex(BIG_MEM),'loader',LOADER)
# bootcompress-20261113: NORD2_BOOT_COMPRESS=nostock packs WITHOUT the Android kernel Image (18.5 MB of the 32 MiB partition, used only by
# the chain-back to Android): a reset-to-LK-fastboot stub takes its place, the payload keeps its address, the loader is unchanged
# (bootcompress-20261113/nord2-pack-xnu-nostock.py, checker nord2-check-xnu-nostock.py, host boot test test/run_host_tests.sh).
# Unset/empty = the stock layout, byte for byte as before. LK already gunzips the kernel: lz4/zstd/xz in the loader are not offered.
BOOT_COMPRESS=_os.environ.get('NORD2_BOOT_COMPRESS','')
BCDIR=Path('$NORD2_WORK/gpu/build/bootcompress-20261113')
if BOOT_COMPRESS not in ('','nostock'):raise SystemExit('NORD2_BOOT_COMPRESS=%r: only "nostock" (or unset) is supported; see bootcompress-20261113/nord2-pack-xnu-nostock.py'%BOOT_COMPRESS)
print('boot image layout:',BOOT_COMPRESS or 'stock (Android kernel kept for the chain-back)')
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
 with (out/(label+'.log')).open('w') as log:subprocess.run([str(a) for a in args],cwd=r,stdout=log,stderr=log,check=True,env={**__import__('os').environ,'NORD2_MEM_SIZE':hex(BIG_MEM),'NORD2_PHYS_BASE':hex(BIG_BASE)})
run('isa',['python3',r/'tools/nord2-check-rcpc2.py',kernel,'--objdump',main/'../toolchains/proton-clang/bin/llvm-objdump'])
# cam60-20261112 (q10jm): the kernel's symbol table is stripped as well (Apple's kernelcaches have none): -S -x kept 6336 global
# symbols (304 KB of __LINKEDIT, ~90 KB of the compressed image); local relocations (KASLR) and function starts are kept, nothing else
# changes. Panic backtraces then show addresses only: symbolicate them offline with the unstripped kernel.
run('strip',[main/'../toolchains/cctools/bin/aarch64-apple-darwin-strip',kernel,'-o',out/'kernel.strip'])
run('payload',['python3',r/'tools/mkpayload.py',out/'kernel.strip',out/'gpu.adt',out/'gpu.payload','--cmdline',cmd])
if BOOT_COMPRESS=='nostock':
 STUB=BCDIR/'stub/stub-Image'
 run('pack',['python3',BCDIR/'nord2-pack-xnu-nostock.py',base,LOADER,out/'gpu.payload',image,'--zopfli',main/'../toolchains/zopfli/zopfli','--stub',STUB])
 run('check',['python3',BCDIR/'nord2-check-xnu-nostock.py',image,out/'gpu.payload','--stock',base,'--stub',STUB,'--loader',LOADER])
else:
 run('pack',['python3',r/'tools/nord2-pack-xnu.py',base,LOADER,out/'gpu.payload',image,'--zopfli',main/'../toolchains/zopfli/zopfli'])
 run('check',['python3',r/'tools/nord2-check-xnu.py',image,out/'gpu.payload','--stock',base])
assert 'FAILURES: none' in (out/'check.log').read_text()
assert sha(kernel)==kernel_sha, 'kernel changed during packing'
record={'image':str(image),'sha256':sha(image),'bytes':image.stat().st_size,'boot_spare':33554432-image.stat().st_size,'kernel':kernel_sha,'adt_baseline':sha(source/'playback.adt'),'adt':sha(out/'gpu.adt'),'commandline':cmd,'device_tested':False,'approved':False,'metal_system_registration':args.system_metal,'kernel_symbols':'stripped (cam60-20261112/pack_with_args_nosyms.py)','phys_base':hex(BIG_BASE),'mem_size':hex(BIG_MEM),'loader':str(LOADER),'loader_sha256':sha(LOADER)}
if BOOT_COMPRESS=='nostock':record.update({'boot_compress':'nostock','stub':str(STUB),'stub_sha256':sha(STUB),'chain_back':'none: loader fatal / XNU chain-back reset to LK fastboot (stub)'})
(out/'image-receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
