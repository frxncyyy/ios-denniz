set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
R=/work/root
echo "=== uicache/installd/lsd/ldid"; ls -la $R/usr/bin/uicache $R/usr/libexec/installd $R/usr/libexec/lsd $R/usr/bin/ldid $R/usr/bin/lsregister 2>&1 | head
echo "=== LaunchServices"; ls -la $R/private/var/mobile/Library/Caches/ 2>&1 | head -40
ls -la $R/private/var/db/ 2>&1 | head -60
echo "=== nord2 state"; ls -la $R/private/var/db/nord2* 2>&1 | head -30
python3 - <<'PY'
import os
from pathlib import Path
R=Path('/work/root')
def size(p):
    t=0
    for dp,dn,fn in os.walk(p,followlinks=False):
        for n in fn:
            try: t+=os.lstat(os.path.join(dp,n)).st_blocks*512
            except OSError: pass
    return t
for rel in ['private/var/db','private/var/staged_system_apps','private/var/tmp','private/var/mobile','private/var/root','private/var/log','usr/standalone/update','System/Library/Caches','System/Library/PreinstalledAssetsV2','private/preboot']:
    p=R/rel
    if not p.is_dir(): print('absent',rel); continue
    print('>>',rel,'%.0f MB'%(size(p)/1e6))
    ch=[]
    for c in p.iterdir():
        if c.is_symlink(): continue
        s=size(c) if c.is_dir() else c.lstat().st_blocks*512
        ch.append((s,c.name))
    for s,n in sorted(ch,reverse=True)[:12]:
        if s>5e6: print('   %8.1f MB  %s'%(s/1e6,n))
PY
echo "=== find nord2 backups"; find $R -maxdepth 6 -name 'updates' -o -maxdepth 6 -iname '*nord2*' -type d 2>/dev/null | head -30
umount $R
echo INSPECT_DONE
