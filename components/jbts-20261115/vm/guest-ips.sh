set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import glob, os, json
R='/work/root'
fs=sorted(glob.glob(R+'/private/var/mobile/Library/Logs/CrashReporter/*DocumentManager*'), key=os.path.getmtime)
print('IPS_FOUND', len(fs), [os.path.basename(f) for f in fs[-5:]])
for f in fs[-2:]:
    t=open(f,errors='replace').read()
    for key in ('"exception"','"asi"','abort() called','plugin has invalid','reason','Last Exception','"termination"'):
        i=t.find(key)
        if i>=0: print('IPS', os.path.basename(f), key, '::', t[i:i+400].replace('\n',' ')[:400])
print('JBTS_STATE', os.path.exists(R+'/private/var/db/nord2-jbts.json'), os.path.isdir(R+'/private/var/jb'), [os.path.basename(p) for p in glob.glob(R+'/private/var/containers/Bundle/Application/*/TrollStore.app')])
for a in ('Terraria.app','SubwaySurf.app'): print('APP_PRESENT', a, os.path.isdir(R+'/Applications/'+a))
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
