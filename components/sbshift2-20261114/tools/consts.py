import sys,struct
sys.path.insert(0,'$NORD2_WORK/gpu/build/sbshift-20261114/tools')
from dsc import *
import img
st=[a for a,n in img.imgs if n.endswith('/UIKitCore')][0]
mm,fo,fn=v2f(st)
ncmds=struct.unpack_from('<I',mm,fo+16)[0]; p=fo+32; secs=[]
for _ in range(ncmds):
    cmd,cs=struct.unpack_from('<II',mm,p)
    if cmd==0x19:
        seg=mm[p+8:p+24].rstrip(b'\0').decode(); ns=struct.unpack_from('<I',mm,p+64)[0]
        for k in range(ns):
            q=p+72+80*k; sn=mm[q:q+16].rstrip(b'\0').decode(); a,sz=struct.unpack_from('<QQ',mm,q+32); secs.append((seg,sn,a,sz))
    p+=cs
for s in secs: print(s[0],s[1],hex(s[2]),hex(s[3]),v2f(s[2])[2] if v2f(s[2]) else None)
