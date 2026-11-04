mount -t hfsplus -o ro,force /dev/vda /work/root || mount -t hfsplus -o ro /dev/vda /work/root
echo "=== SystemVersion"; cat /work/root/System/Library/CoreServices/SystemVersion.plist | head -30
echo "=== df"; df -k /work/root
echo "=== top-level sizes (KiB)"
python3 - <<'PY'
import os
from pathlib import Path
root=Path('/work/root')
def size(p):
    t=0
    for dp,dn,fn in os.walk(p, followlinks=False):
        for n in fn:
            try:
                st=os.lstat(os.path.join(dp,n)); t+=st.st_blocks*512
            except OSError: pass
    return t
def top(p,depth,minmb):
    out=[]
    for c in sorted(p.iterdir()) if p.is_dir() else []:
        if c.is_symlink() or not c.is_dir(): continue
        s=size(c)
        if s>=minmb<<20:
            out.append((s,c))
    for s,c in sorted(out,reverse=True):
        print('%8.1f MB  %s'%(s/1e6,c.relative_to(root)))
        if depth>1: top(c,depth-1,max(minmb,30))
top(root,3,100)
PY
echo "=== Applications"; ls -la /work/root/Applications | head -60
echo "=== containers"; ls -la /work/root/private/var/containers 2>&1 | head; ls /work/root/private/var/containers/Bundle/Application 2>&1 | head
echo "=== uicache/installd/lsd"; ls -la /work/root/usr/bin/uicache /work/root/usr/libexec/installd /work/root/usr/libexec/lsd /work/root/usr/bin/ldid /work/root/usr/bin/jtool 2>&1 | head
echo "=== LaunchServices db"; ls -la /work/root/private/var/mobile/Library/Caches/com.apple.LaunchServices* /work/root/private/var/db/lsd 2>&1 | head -20
echo "=== launchd daemons listing"; ls /work/root/System/Library/LaunchDaemons | head -80
umount /work/root
echo INSPECT_DONE
