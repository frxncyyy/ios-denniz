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
args=args.parse_args()
r=Path(__file__).resolve().parents[2];main=Path('$NORD2_REPO');out=args.output_dir.resolve() if args.output_dir else r/('gpu/build/device-ui-candidate' if args.system_metal else 'gpu/build/device-candidate');out.mkdir(exist_ok=True)
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
(out/'gpu.adt').write_bytes(blob)
ci=json.loads((source/'build-result.json').read_text());cmd=ci['images'][0]['commandline'].replace(' nord2fbdiag=1','')+f' nord2_gpu=1 nord2_metal={int(args.system_metal)}';assert len(cmd)<608
kernel=args.kernel.resolve() if args.kernel else r/'gpu/build/kernel-native/kernel.development.nord2-metal';base=main/'research/nord2/rescue/recompressed/base.img'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
kernel_sha=sha(kernel)
assert sha(base)=='ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf'
def run(label,args):
 with (out/(label+'.log')).open('w') as log:subprocess.run([str(a) for a in args],cwd=r,stdout=log,stderr=log,check=True)
run('isa',['python3',r/'tools/nord2-check-rcpc2.py',kernel,'--objdump',main/'../toolchains/proton-clang/bin/llvm-objdump'])
run('strip',[main/'../toolchains/cctools/bin/aarch64-apple-darwin-strip','-S','-x',kernel,'-o',out/'kernel.strip'])
run('payload',['python3',r/'tools/mkpayload.py',out/'kernel.strip',out/'gpu.adt',out/'gpu.payload','--cmdline',cmd])
run('pack',['python3',r/'tools/nord2-pack-xnu.py',base,source/'loader/loader-Image',out/'gpu.payload',image,'--zopfli',main/'../toolchains/zopfli/zopfli'])
run('check',['python3',r/'tools/nord2-check-xnu.py',image,out/'gpu.payload','--stock',base])
assert 'FAILURES: none' in (out/'check.log').read_text()
assert sha(kernel)==kernel_sha, 'kernel changed during packing'
record={'image':str(image),'sha256':sha(image),'bytes':image.stat().st_size,'boot_spare':33554432-image.stat().st_size,'kernel':kernel_sha,'adt_baseline':sha(source/'playback.adt'),'adt':sha(out/'gpu.adt'),'commandline':cmd,'device_tested':False,'approved':False,'metal_system_registration':args.system_metal}
(out/'image-receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
