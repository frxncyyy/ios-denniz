set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
R=/work/root
ls $R/private/var/mobile/Library/Logs/CrashReporter | grep -i "location\|clt\|cl" | head
ls -la $R/System/Library/LaunchDaemons/com.apple.locationd.plist $R/usr/libexec/locationd 2>&1
python3 - <<'PY'
import plistlib,json
d=plistlib.loads(open('/work/root/System/Library/xpc/launchd.plist','rb').read())['LaunchDaemons']
for k,v in d.items():
    if 'location' in k.lower() or 'gamed' in k.lower() or 'locationd' in json.dumps(v,default=str):
        print(k, json.dumps(v,default=str)[:600])
PY
ls $R/private/var/mobile/Library/Caches/ | grep -i "locat" ; ls -la $R/private/var/root/Library/Caches/locationd 2>&1 | head; ls $R/private/var/root/Library/Preferences | grep -i locat
umount $R
echo INSPECT_DONE
