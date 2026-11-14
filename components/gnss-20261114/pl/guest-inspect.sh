set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, plistlib, json
R='/work/root'; cfg=plistlib.loads(open(R+'/System/Library/xpc/launchd.plist','rb').read())
for k,j in cfg['LaunchDaemons'].items():
    if 'nord2' in k or 'n2' in str(j.get('ProgramArguments',''))[:80]:
        print('INSPECT_JOB', k, json.dumps(j.get('ProgramArguments')), 'RunAtLoad', j.get('RunAtLoad'), 'KeepAlive', j.get('KeepAlive'))
for p in ['/usr/local/libexec/n2mnld','/usr/local/libexec/n2locbridge','/usr/local/libexec/n2gpsdl_step','/private/var/db/n2gnss','/private/var/db/nord2-gnssd','/private/var/db/nord2-gnssd.json',
          '/private/var/db/nord2-gnssstep','/private/var/db/nord2-gnssstep.off-gnssd1','/private/var/db/nord2-locbridge','/private/var/db/nord2-locbridge.json','/private/var/run/n2gnss.nmea',
          '/private/var/db/nord2-locfix','/usr/local/share/n2gnss','/private/var/db/nord2-metal/receipt.json']:
    print('INSPECT_PATH', p, os.path.exists(R+p), (os.listdir(R+p) if os.path.isdir(R+p) else (os.path.getsize(R+p) if os.path.exists(R+p) else '')))
for p in ['/private/var/db/nord2-gnssd.json','/private/var/db/nord2-locbridge.json']:
    if os.path.exists(R+p): print('INSPECT_REC', p, open(R+p).read().replace('\n',' ')[:400])
lp='/private/var/mobile/Library/Preferences/com.apple.locationd.plist'
for q in ['/private/var/root/Library/Preferences/com.apple.locationd.plist','/private/var/mobile/Library/Preferences/com.apple.locationd.plist']:
    if os.path.exists(R+q):
        try: d=plistlib.loads(open(R+q,'rb').read()); print('INSPECT_LOCD', q, {k:d[k] for k in list(d)[:12]})
        except Exception as e: print('INSPECT_LOCD', q, 'unreadable', e)
GUEST
umount /work/root
echo INSTALL_DONE
