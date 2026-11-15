#!/usr/bin/env python3
"""0790: apply the two archive-copy symbol NAME edits (same rule as n8x_rename in build_p1_candidate_integ_cpu.py) to extracted CI
members and check the result. usage: rename_check.py CI_DIR OUT_DIR"""
import sys,hashlib,subprocess
from pathlib import Path
ci=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
PAIRS={'wifi_ios_control.o':[('niw_decode','niv_decode')],'wifi_ios_station_view.o':[('niwj_state_reply','niwk_state_reply')]}
for m,pairs in PAIRS.items():
    blob=(ci/m).read_bytes();before=hashlib.sha256(blob).hexdigest()
    for old,new in pairs:
        o,n=b'\0_'+old.encode()+b'\0',b'\0_'+new.encode()+b'\0'
        assert len(o)==len(n) and blob.count(o)==1 and n not in blob,(m,old,blob.count(o))
        blob=blob.replace(o,n)
    (out/m).write_bytes(blob)
    nm=subprocess.run(['llvm-nm',str(out/m)],capture_output=True,text=True,check=True).stdout
    import struct
    old=(ci/m).read_bytes();ncmds=struct.unpack_from('<I',old,16)[0];off=32;strtab=None
    for _ in range(ncmds):
        cmd,size=struct.unpack_from('<II',old,off)
        if cmd==2: so,ss=struct.unpack_from('<II',old,off+16);strtab=(so,so+ss)
        off+=size
    diffs=[i for i in range(len(old)) if old[i]!=blob[i]]
    same=len(old)==len(blob) and strtab and all(strtab[0]<=i<strtab[1] for i in diffs)
    print(m,'sha before',before[:16],'after',hashlib.sha256(blob).hexdigest()[:16],'| %d bytes changed, all inside the string table:'%len(diffs),bool(same))
    print('   ',' '.join(l.split()[-1]+'('+l.split()[-2]+')' for l in nm.split('\n') if any(p in l for p in ('decode','state_reply'))))
    assert same
print('RENAME_OK')
