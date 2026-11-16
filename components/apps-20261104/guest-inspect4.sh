set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
R=/work/root
echo "=== installd"; find $R/private/var/installd -maxdepth 5 | head -40
echo "=== csstore"; find $R/private/var -name '*.csstore' 2>/dev/null | head; find $R/private/var -maxdepth 4 -iname '*LaunchServices*' 2>/dev/null | head
echo "=== folders"; ls $R/private/var/folders 2>&1 | head
echo "=== mobile Containers"; find $R/private/var/mobile/Containers -maxdepth 3 | head -20
echo "=== MobileInstallation plist keys"; python3 - <<'PY'
import plistlib,glob
for p in glob.glob('/work/root/private/var/installd/Library/MobileInstallation/*')+glob.glob('/work/root/private/var/mobile/Library/MobileInstallation/*'):
    print(p)
PY
echo "=== diagnostics sizes"; du -sk $R/private/var/db/diagnostics/* 2>/dev/null
echo "=== SpringBoard prefs/how launched"; python3 - <<'PY'
import plistlib
d=plistlib.loads(open('/work/root/System/Library/xpc/launchd.plist','rb').read())['LaunchDaemons']
for k,v in d.items():
    if 'nord2' in k.lower() or 'tegu' in k.lower() or 'sweet' in k.lower(): print(k, v)
PY
echo "=== AMFI/cs"; ls $R/System/Library/LaunchDaemons | grep -i "amfi\|cs\|trust" ; ls $R/usr/libexec | grep -i "amfi\|trust"
echo "=== Info of a system app (Camera)"; ls $R/Applications/Camera.app | head -20
umount $R
echo INSPECT_DONE
