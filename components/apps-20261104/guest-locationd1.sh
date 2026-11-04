set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# locationd1: Location Services master switch ON + Camera pre-authorised "While Using" (no Camera location alert at first launch).
#  1. /private/var/mobile/Library/Preferences/com.apple.locationd.plist  LocationServicesEnabledIn8.0 = true
#     (key string verified in /usr/libexec/locationd 20H392; this file is locationd's own prefs: it holds its
#      LastSystemVersion/ObsoleteDataDeleted, written by locationd, root's Preferences has no com.apple.locationd.plist)
#  2. /private/var/root/Library/Caches/locationd/clients.plist  ["com.apple.camera"]["Authorization"] = 2
#     (entry already created by locationd with SupportedAuthorizationMask 3 = Never|WhileUsing; 2 = While Using: inferred)
# Only those two keys change; every other key of both files is kept as found. Previous values -> /private/var/db/nord2-locationd/.
import os, plistlib, json, hashlib
R='/work/root'
P=R+'/private/var/mobile/Library/Preferences/com.apple.locationd.plist'
C=R+'/private/var/root/Library/Caches/locationd/clients.plist'
K=R+'/private/var/db/nord2-locationd'; M=K+'/receipt.json'
KEY='LocationServicesEnabledIn8.0'; APP='com.apple.camera'; AUTH=2
sha=lambda b: hashlib.sha256(b).hexdigest()
def load(p):
    b=open(p,'rb').read(); d=plistlib.loads(b)
    assert isinstance(d, dict), '%s is not a dictionary'%p
    return b, d, (plistlib.FMT_BINARY if b[:8]==b'bplist00' else plistlib.FMT_XML)
def put(path, data, mode, uid, gid):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
# ---- check everything first: nothing is written unless all of it can be
for p in (P, C): assert os.path.isfile(p), 'missing %s (locationd never ran?): refuse'%p
pb, pd, pf = load(P); cb, cd, cf = load(C)
cam=cd.get(APP)
assert cam is None or isinstance(cam, dict), 'clients.plist %s is not a dictionary: %r'%(APP, cam)
if cam is not None and 'Authorization' in cam:
    assert cam['Authorization'] in (0, AUTH), 'Camera already has Authorization %r (someone decided): refuse'%cam['Authorization']
done = pd.get(KEY) is True and cam is not None and cam.get('Authorization')==AUTH
if done:
    assert os.path.isfile(M), 'already enabled/authorised but no receipt %s: review'%M
    print('LOCATIOND1_ALREADY', KEY, pd.get(KEY), APP, cam)
else:
    rec={'prefs_key_before': pd.get(KEY, '<absent>'), 'camera_entry_before': cam if cam is not None else '<absent>',
         'prefs_sha_before': sha(pb), 'clients_sha_before': sha(cb)}
    if os.path.isfile(M):        # a half-done earlier run: keep its (older) record of the original state
        rec=json.load(open(M))
    os.makedirs(K, exist_ok=True); os.chmod(K, 0o755)
    ps=os.stat(P); cs=os.stat(C)
    if not os.path.exists(K+'/com.apple.locationd.plist.pre-locationd1'): put(K+'/com.apple.locationd.plist.pre-locationd1', pb, 0o600, 0, 0)
    if not os.path.exists(K+'/clients.plist.pre-locationd1'): put(K+'/clients.plist.pre-locationd1', cb, 0o600, 0, 0)
    put(M, (json.dumps(rec, indent=1, sort_keys=True, default=str)+'\n').encode(), 0o644, 0, 0)
    pd[KEY]=True
    ncam=dict(cam) if cam is not None else {'SupportedAuthorizationMask': 3}
    ncam['Authorization']=AUTH; cd[APP]=ncam
    nb=plistlib.dumps(pd, fmt=pf); put(P, nb, ps.st_mode&0o7777, ps.st_uid, ps.st_gid)
    nc=plistlib.dumps(cd, fmt=cf); put(C, nc, cs.st_mode&0o7777, cs.st_uid, cs.st_gid)
    # read back: only the two keys differ
    a=plistlib.loads(open(P,'rb').read()); b=plistlib.loads(open(C,'rb').read())
    assert a[KEY] is True and {k:v for k,v in a.items() if k!=KEY}=={k:v for k,v in plistlib.loads(pb).items() if k!=KEY}
    oc=plistlib.loads(cb); assert {k:v for k,v in b.items() if k!=APP}=={k:v for k,v in oc.items() if k!=APP}
    assert b[APP]['Authorization']==AUTH
    print('LOCATIOND1_INSTALLED', KEY, '=', True, '(was %r);'%rec['prefs_key_before'], APP, '->', b[APP], '(was %r)'%rec['camera_entry_before'])
    print('LOCATIOND1_SHA prefs', sha(nb)[:12], 'clients', sha(nc)[:12])
print('CC_JOB_ADDED location services on, Camera authorised While Using (com.apple.locationd.plist, locationd/clients.plist)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
