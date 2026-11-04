set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
R=/work/root/private/var/db/nord2-metal
echo "=== receipt"; cat $R/receipt.json
echo "=== updates"; ls -la $R/updates
for u in $R/updates/*; do echo "--- $u"; ls -la $u; python3 - "$u" <<'PY'
import os,sys
t=0;big=[]
for dp,dn,fn in os.walk(sys.argv[1]):
    for n in fn:
        p=os.path.join(dp,n); s=os.lstat(p).st_blocks*512; t+=s; big.append((s,p))
print('TOTAL %.1f MB'%(t/1e6)); 
for s,p in sorted(big,reverse=True)[:8]: print('  %.1f MB %s'%(s/1e6,p.split('updates/')[1]))
PY
done
echo "=== diagnostics"; ls -la /work/root/private/var/db/diagnostics | head -20
echo "=== system version of Applications dir for third party"; ls /work/root/private/var/containers/Bundle; ls /work/root/private/var/containers/Data
echo "=== LS dbs"; find /work/root/private/var -maxdepth 6 \( -name 'com.apple.LaunchServices*' -o -name 'MobileInstallation*' -o -name 'installd*' -o -name 'LSApplication*' \) 2>/dev/null | head
echo "=== launchd installd/lsd/mobile_installation"; python3 - <<'PY'
import plistlib
d=plistlib.loads(open('/work/root/System/Library/xpc/launchd.plist','rb').read())['LaunchDaemons']
print(len(d))
for k,v in d.items():
    if any(x in k for x in ('install','lsd','LaunchServices','mobile_installation','uicache','containermanager','amfid','trustd','SpringBoard','backboardd','mobile.installd','lsd')): print(k, v.get('ProgramArguments') or v.get('Program'), v.get('RunAtLoad'))
PY
umount /work/root
echo INSPECT_DONE
