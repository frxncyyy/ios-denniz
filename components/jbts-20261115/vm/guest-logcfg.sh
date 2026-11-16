set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import os, plistlib, json, glob
R='/work/root'
for d in ('/Library/Preferences/Logging','/Library/Preferences/Logging/Subsystems','/System/Library/Preferences/Logging','/System/Library/Preferences/Logging/Subsystems'):
    print('LOGDIR', d, os.path.isdir(R+d), (sorted(os.listdir(R+d))[:12] if os.path.isdir(R+d) else ''), len(os.listdir(R+d)) if os.path.isdir(R+d) else 0)
for n in ('com.apple.FileProvider.plist','com.apple.DocumentManager.plist','com.apple.pluginkit.plist'):
    p=R+'/System/Library/Preferences/Logging/Subsystems/'+n
    if os.path.exists(p): print('SUBSYS', n, json.dumps(plistlib.load(open(p,'rb')),default=str)[:500])
p=R+'/System/Library/Preferences/Logging/com.apple.system.logging.plist'
if os.path.exists(p): print('SYSLOG', json.dumps(plistlib.load(open(p,'rb')),default=str)[:500])
ex=sorted(glob.glob(R+'/System/Library/Preferences/Logging/Subsystems/*.plist'))[:3]
for e in ex: print('EX', e[len(R):], json.dumps(plistlib.load(open(e,'rb')),default=str)[:300])
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
