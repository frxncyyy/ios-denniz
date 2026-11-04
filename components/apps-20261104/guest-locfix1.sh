set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# locfix1: /usr/libexec/locationd replaced by the patched copy (gpu/hwcodec/locfix/patch_locationd.py): the three calls of
# +[CLLocationManager authorizationStatusForBundle:/ForBundlePath:] inside locationd return 3 instead of asking locationd
# itself over com.apple.locationd.synchronous (self-deadlock in CLEmergencyController's constructor, seen by locdiag1).
# The original is kept as /private/var/db/nord2-locfix/locationd.orig (receipt.json next to it). Nothing else changes.
import os, json, hashlib
R='/work/root'; P='/usr/libexec/locationd'; K='/private/var/db/nord2-locfix'; BAK=K+'/locationd.orig'; REC=K+'/receipt.json'
ORIG='75066240cfcc9792b2314bc90847811d164cf702756890078bbd42131f9ef4b9'
plan=json.load(open('/src/plan.json')); NEW=plan['files']['locationd']
sha=lambda p: hashlib.sha256(open(R+p,'rb').read()).hexdigest()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert hashlib.sha256(open(R+path,'rb').read()).digest()==hashlib.sha256(data).digest()
new=open('/src/locationd','rb').read(); assert hashlib.sha256(new).hexdigest()==NEW, 'payload locationd is not the planned one'
cur=sha(P)
if cur==NEW:
    assert os.path.isfile(R+BAK) and sha(BAK)==ORIG, 'patched locationd without the original kept: review'
    print('LOCFIX1_ALREADY', NEW[:12])
else:
    assert cur==ORIG, 'locationd is neither the 20H392 original nor the patched one: %s'%cur
    st=os.stat(R+P)
    if not os.path.isdir(R+K): os.mkdir(R+K); os.chmod(R+K,0o755); os.chown(R+K,0,0)
    if not (os.path.isfile(R+BAK) and sha(BAK)==ORIG): put(BAK, open(R+P,'rb').read(), 0o644, 0, 0)
    put(P, new, st.st_mode&0o7777, st.st_uid, st.st_gid)
    put(REC, (json.dumps({'orig':ORIG,'new':NEW,'mode':oct(st.st_mode&0o7777),'uid':st.st_uid,'gid':st.st_gid},indent=1)+'\n').encode(), 0o644, 0, 0)
    print('LOCFIX1_INSTALLED', ORIG[:12], '->', NEW[:12], 'mode', oct(st.st_mode&0o7777))
print('CC_JOB_ADDED locationd self-deadlock patch (locfix1)'); print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
