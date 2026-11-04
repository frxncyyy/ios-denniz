set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
R=/work/root
echo "=== crash dirs"; ls -la $R/private/var/mobile/Library/Logs/CrashReporter 2>&1 | head -30; ls -la $R/private/var/mobile/Library/Logs/CrashReporter/Retired 2>&1 | head
find $R/private/var -iname '*SubwaySurf*' -o -iname '*Terraria*' 2>/dev/null | grep -v "^$R/private/var/containers" | head -20
echo "=== app data containers"; python3 - <<'PY'
import os,plistlib,glob
for base in ('/work/root/private/var/mobile/Containers/Data/Application',):
    for d in glob.glob(base+'/*'):
        m=d+'/.com.apple.mobile_container_manager.metadata.plist'
        try: b=plistlib.load(open(m,'rb')).get('MCMMetadataIdentifier','')
        except Exception: continue
        if 'kiloo' in b or '505games' in b: print(d,b)
PY
umount $R
echo INSPECT_DONE
