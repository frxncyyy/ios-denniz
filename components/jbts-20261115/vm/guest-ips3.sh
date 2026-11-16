set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import os, hashlib, plistlib, time
R='/work/root'
for app in ('SubwaySurf.app','Terraria.app'):
    d=R+'/Applications/'+app; ip=plistlib.load(open(d+'/Info.plist','rb')); exe=d+'/'+ip['CFBundleExecutable']
    st=os.stat(exe)
    print('APPX', app, ip.get('CFBundleIdentifier'), ip.get('CFBundleShortVersionString'), ip.get('CFBundleVersion'), 'exe', ip['CFBundleExecutable'], st.st_size, hashlib.sha256(open(exe,'rb').read()).hexdigest(), time.strftime('%Y-%m-%d %H:%M', time.gmtime(st.st_mtime)), oct(st.st_mode), st.st_uid)
    others=[n for n in os.listdir(d) if 'pre' in n.lower() or 'orig' in n.lower() or 'nord2' in n.lower() or n.endswith('.bak')]
    print('APPX', app, 'extras', others[:20])
    newest=sorted(((os.stat(os.path.join(b,f)).st_mtime, os.path.join(b,f)[len(d):]) for b,_,fs in os.walk(d) for f in fs))[-5:]
    print('APPX', app, 'newest', [(time.strftime('%m-%d %H:%M', time.gmtime(t)), p) for t,p in newest])
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
