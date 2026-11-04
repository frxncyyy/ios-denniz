set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# undo of guest-locationd1.sh: the two keys back to what the receipt recorded (absent -> removed); other keys as found now
# (locationd rewrites both files while it runs, so the old copies are NOT copied back). Receipt dir removed.
import os, plistlib, json, shutil
R='/work/root'
P=R+'/private/var/mobile/Library/Preferences/com.apple.locationd.plist'
C=R+'/private/var/root/Library/Caches/locationd/clients.plist'
K=R+'/private/var/db/nord2-locationd'; M=K+'/receipt.json'
KEY='LocationServicesEnabledIn8.0'; APP='com.apple.camera'
def load(p):
    b=open(p,'rb').read(); return plistlib.loads(b), (plistlib.FMT_BINARY if b[:8]==b'bplist00' else plistlib.FMT_XML)
def put(path, data, mode, uid, gid):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
if not os.path.isfile(M):
    assert not os.path.exists(K), '%s exists without receipt: review'%K
    print('LOCATIOND1_UNDO_ALREADY (no receipt)')
else:
    rec=json.load(open(M))
    pd, pf = load(P); cd, cf = load(C)
    pk=rec['prefs_key_before']; ce=rec['camera_entry_before']
    if pk=='<absent>': pd.pop(KEY, None)
    else: pd[KEY]=pk
    if ce=='<absent>': cd.pop(APP, None)
    else:
        cam=dict(cd.get(APP, {}))
        if 'Authorization' in ce: cam['Authorization']=ce['Authorization']
        else: cam.pop('Authorization', None)
        cd[APP]=cam
    ps=os.stat(P); cs=os.stat(C)
    put(P, plistlib.dumps(pd, fmt=pf), ps.st_mode&0o7777, ps.st_uid, ps.st_gid)
    put(C, plistlib.dumps(cd, fmt=cf), cs.st_mode&0o7777, cs.st_uid, cs.st_gid)
    shutil.rmtree(K)
    print('LOCATIOND1_UNDONE', KEY, '->', pd.get(KEY, '<absent>'), APP, '->', cd.get(APP, '<absent>'))
print('CC_JOB_ADDED undo locationd1')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
