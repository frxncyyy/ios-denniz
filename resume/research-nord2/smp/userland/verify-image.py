from pathlib import Path
import hashlib, importlib.util, struct, json, sys
sys.path.insert(0, 'tools')
import adt
spec=importlib.util.spec_from_file_location('pack', 'tools/nord2-pack-xnu.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
out=Path('research/nord2/smp/userland')
image=Path('shim/out/nord2-xnu/nord2-smp-usb-userland-stop21.img')
sha=lambda b:hashlib.sha256(b).hexdigest()
stock=Path('backup/nord2/boot.img').read_bytes()
assert sha(stock)=='04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01'
rescue=Path('research/nord2/rescue/nord2-adb-rescue.img').read_bytes()
assert sha(rescue)=='d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'
root=Path('research/nord2/rootfs-live/nord2-hfs-pid1.img').read_bytes()
assert sha(root)=='b5fa35f397e784c1da04988503ee43d9ce23e7925829a84be181d4ceba07308a'
_,skg,_,sdt=m.stock_parts(stock);_,_,rrd,rdt=m.stock_parts(rescue)
si,sapp=m.unpack_stock_kernel(skg);size=struct.unpack_from('<Q',si,16)[0]
poff=(0x200000+size+0x3fff)&~0x3fff
data=image.read_bytes();assert len(data)<=33554432
_,kg,rd,dt=m.stock_parts(data);blob,app=m.unpack_stock_kernel(kg)
assert app==sapp and dt==sdt==rdt and rd==rrd
assert blob[0x200000:0x200000+len(si)]==si
assert blob[0x200000+len(si):0x200000+size]==bytes(size-len(si))
payload=blob[poff:];assert payload[:8]==b'SWEETXNU'
assert 'FAILURES: none' in (out/'image-build.log').read_text()
cmd=payload[0x48:0x48+608].split(b'\0')[0].decode()
assert cmd=='debug=0xc cpus=8 keepsyms=1 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 nord2stopat=21 rd=md0 ufsrw=0 rootrw=0'
vb,entry,ao,asz,kend,pb,ms=struct.unpack_from('<7Q',payload,0x10)
a,_=adt.parse(payload[ao:ao+asz])
chosen=next(n for n in a.children if n.name=='chosen')
mm=next(n for n in chosen.children if n.name=='memory-map')
rpa,rsz=struct.unpack('<QQ', mm.prop('RAMDisk'))
segs=[struct.unpack_from('<4Q',payload,0x2a8+i*32) for i in range(struct.unpack_from('<I',payload,12)[0])]
rsegs=[s for s in segs if s[0]==rpa-pb+vb]
assert len(rsegs)==1
_,vsz,off,filesz=rsegs[0]
assert vsz==rsz==filesz==len(root) and payload[off:off+filesz]==root
(out/'payload.bin').write_bytes(payload)
report=dict(image=str(image),image_bytes=len(data),boot_spare_bytes=33554432-len(data),image_sha256=sha(data),
 payload_sha256=sha(payload),cmdline=cmd,xnu_cpus=8,original_linux_kernel_and_dt_unchanged=True,
 tested_rescue_ramdisk_unchanged=True,diagnostic_ram_root_embedded_exactly=True,diagnostic_ram_root_sha256=sha(root),
 point=21,usb_seconds=45,expected_pid1_ticks=10,expected_pid1_exit=66,disk_writes_enabled=False,
 kernel_source='full Apple-clang CI39437387549, source1f5efba, generic ARMv8.2+crypto',hardware_tested=False)
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
