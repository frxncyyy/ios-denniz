#!/usr/bin/env python3
"""Offline exact-CPIO recompression; never accesses hardware or overwrites rescue."""
import argparse,gzip,hashlib,importlib.util,json,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
SOURCE=ROOT/'research/nord2/rescue/nord2-adb-rescue.img'
def sha(b):return hashlib.sha256(b).hexdigest()
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def main():
    ap=argparse.ArgumentParser();ap.add_argument('zopfli',type=Path);a=ap.parse_args()
    rescue=module('rescue',ROOT/'tools/nord2-mkrescue.py')
    pack=module('pack',ROOT/'tools/nord2-pack-xnu.py')
    original=SOURCE.read_bytes()
    assert sha(original)=='d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'
    header,(kernel,ramdisk,dtb)=rescue.parts(original)
    page,k2,r2,d2=pack.stock_parts(original)
    assert page==2048 and (kernel,ramdisk,dtb)==(k2,r2,d2)
    linux,appended_dt=pack.unpack_stock_kernel(kernel)
    assert appended_dt and dtb
    raw=gzip.decompress(ramdisk);assert len(ramdisk)==7710128 and len(raw)==19476076
    # Structural check only; do NOT rebuild/reorder/normalize the CPIO archive.
    entries=rescue.cpio_read(raw)
    rawpath=OUT/'ramdisk.cpio';rawpath.write_bytes(raw)
    compressedpath=OUT/'ramdisk.cpio.gz'
    assert not compressedpath.exists(),'refuse overwriting previous compression result'
    command=[str(a.zopfli.resolve()),'--gzip','--i15',str(rawpath)]
    subprocess.run(command,check=True)
    compressed=compressedpath.read_bytes()
    assert gzip.decompress(compressed)==raw
    manifest=dict(source=str(SOURCE.relative_to(ROOT)),source_bytes=len(original),source_sha256=sha(original),
       command=command,iterations=15,original_ramdisk_bytes=len(ramdisk),original_ramdisk_sha256=sha(ramdisk),
       ramdisk_bytes=len(compressed),ramdisk_sha256=sha(compressed),ramdisk_uncompressed_bytes=len(raw),
       ramdisk_uncompressed_sha256=sha(raw),cpio_entries=len(entries),
       kernel_compressed_sha256=sha(kernel),linux_decompressed_sha256=sha(linux),
       appended_dt_bytes=len(appended_dt),appended_dt_sha256=sha(appended_dt),
       header_dt_bytes=len(dtb),header_dt_sha256=sha(dtb),roundtrip_byte_exact=True,hardware_tested=False)
    if len(compressed)<len(ramdisk):
        new_header=bytearray(header);struct.pack_into('<I',new_header,16,len(compressed))
        digest=hashlib.sha1()
        for data in (kernel,compressed,b'',b'',dtb):
            digest.update(data);digest.update(struct.pack('<I',len(data)))
        new_header[576:608]=digest.digest().ljust(32,b'\0')
        image=bytes(new_header)
        for data in (kernel,compressed,dtb):image+=data+bytes(-len(data)%page)
        nh,(nk,nr,nd)=rescue.parts(image)
        assert nk==kernel and nd==dtb and nr==compressed and gzip.decompress(nr)==raw
        nl,na=pack.unpack_stock_kernel(nk);assert nl==linux and na==appended_dt
        allowed=set(range(16,20))|set(range(576,608))
        assert all(x==y or i in allowed for i,(x,y) in enumerate(zip(header,nh)))
        assert len(image)<len(original) and len(image)<=33554432
        verify=hashlib.sha1()
        for part in (nk,nr,b'',b'',nd):verify.update(part);verify.update(struct.pack('<I',len(part)))
        assert nh[576:608]==verify.digest().ljust(32,b'\0')
        (OUT/'base.img').write_bytes(image)
        manifest.update(base_bytes=len(image),base_sha256=sha(image),image_gain_bytes=len(original)-len(image),
          ramdisk_gain_bytes=len(ramdisk)-len(compressed),header_id=nh[576:608].hex(),
          unchanged_linux_and_both_dts=True,base_created=True)
    else:manifest.update(base_created=False,reason='not smaller')
    assert SOURCE.read_bytes()==original
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
