set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
python3 - <<'PY'
import plistlib,json
for n in ('com.apple.CommCenter.plist','com.apple.CommCenter-noncellular.plist'):
    d=plistlib.loads(open('/work/root/System/Library/LaunchDaemons/'+n,'rb').read())
    print('PLIST',n,json.dumps(d,default=str)[:1800])
import os
print('BIN',[ (p,os.path.exists('/work/root'+p)) for p in ('/System/Library/Frameworks/CoreTelephony.framework/Support/CommCenter',)])
cfg=plistlib.loads(open('/work/root/System/Library/xpc/launchd.plist','rb').read())
print('TOPKEYS',list(cfg.keys()))
d=cfg['LaunchDaemons']
k=next(iter(d)); print('SAMPLEKEY',k, list(d[k].keys())[:12])
print('IN_CACHE_NONCELL','/System/Library/LaunchDaemons/com.apple.CommCenter-noncellular.plist' in d)
PY
umount /work/root
echo INSPECT_DONE
