set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os, plistlib
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
NEW='b66f008bdf095aa818db90651bdee3f3103f076bf3d4aa15e42f8b449ac2140e'      # camera-userland-20261109/system/D22/AVCaptureSession.plist: wide :0, ultra-wide :5, DUAL WIDE :6, front :1
CAM3='bda9b23b0dacb01a36fc9301502ac77a356f83333609bd0eba2917bee8db2729'   # the three-camera file (guest-cam3.sh)
SETUP='457d35df2713143e29d150cef39d898d747e0301a5b73cba2c93f171ea852880'  # CameraSetup.plist with PortTypeBackSuperWide (not changed here)
PREFS=R+'/private/var/mobile/Library/Preferences/com.apple.camera.plist'
# what Camera.app, told it runs on an iPhone 11, is not to ask this capture device for (CAMUserPreferences' keys, CameraUI 16.7)
WANT={'CAMUserPreferenceEnableResponsiveShutter': False, 'CAMUserPreferenceEnableSpatialOverCaptureForPhoto': False,
      'CAMUserPreferenceEnableSpatialOverCaptureForVideo': False, 'CAMUserPreferenceEnableViewOutsideTheFrame': False,
      'CAMUserPreferenceAutoLowLightVideoEnabled': False, 'CAMUserPreferenceActionModeLowLightEnabled': False,
      'CAMUserPreferenceContentAwareDistortionCorrection': False, 'CAMUserPreferenceEnableSemanticDevelopment': False,
      'CAMUserPreferenceDidPerformAutoLivePhotoUpgrade': True}
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def put(path, data, mode, uid, gid):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
# ---- check everything first: nothing is written unless all of it can be
cur=D+'/AVCaptureSession.plist'; bak=cur+'.pre-camdual'
new=open('/src/D22/AVCaptureSession.plist','rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the plist this script was written for'
av=plistlib.loads(new); kinds=[(d.get('uniqueID','').rsplit(':',1)[-1], d.get('deviceType')) for d in av['AVCaptureDevices']]
assert ('6',8) in kinds and ('5',7) in kinds and ('0',None) in kinds, 'payload has no dual-wide entry: %r'%kinds
ch=sha(cur)
assert ch in (NEW, CAM3), 'D22/AVCaptureSession.plist is neither the three-camera file nor the dual-wide one: %s (run guest-cam3.sh first)'%ch
assert sha(D+'/CameraSetup.plist')==SETUP, 'D22/CameraSetup.plist is not the one with PortTypeBackSuperWide: %s'%sha(D+'/CameraSetup.plist')
if ch==NEW: assert os.path.exists(bak) and sha(bak)==CAM3, 'dual-wide plist there but no three-camera copy (*.pre-camdual): review'
pd=os.path.dirname(PREFS); assert os.path.isdir(pd), 'no '+pd
pbak=PREFS+'.pre-camdual'; pabsent=PREFS+'.pre-camdual-absent'
had=os.path.exists(PREFS)
prefs=plistlib.loads(open(PREFS,'rb').read()) if had else {}
assert isinstance(prefs, dict)
# ---- the plist
if ch==NEW:
    print('CAMDUAL_PLIST_ALREADY', NEW[:12])
else:
    st=os.stat(cur)
    put(bak, open(cur,'rb').read(), st.st_mode&0o7777, st.st_uid, st.st_gid); assert sha(bak)==CAM3
    put(cur, new, st.st_mode&0o7777, st.st_uid, st.st_gid); assert sha(cur)==NEW
    print('CAMDUAL_PLIST_INSTALLED', NEW[:12], 'devices', kinds, '(three-camera file kept as AVCaptureSession.plist.pre-camdual)')
# ---- the preferences
changed={k:v for k,v in WANT.items() if prefs.get(k, None) is not v}
if not changed:
    print('CAMDUAL_PREFS_ALREADY', len(WANT))
else:
    if had: st=os.stat(PREFS); mode,uid,gid=st.st_mode&0o7777,st.st_uid,st.st_gid
    else: st=os.stat(pd); mode,uid,gid=0o600,st.st_uid,st.st_gid
    if not os.path.exists(pbak) and not os.path.exists(pabsent):
        if had: put(pbak, open(PREFS,'rb').read(), mode, uid, gid)
        else: put(pabsent, b'', mode, uid, gid)
    was={k: prefs.get(k, '(not set)') for k in changed}
    prefs.update(WANT)
    put(PREFS, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), mode, uid, gid)
    assert all(plistlib.loads(open(PREFS,'rb').read()).get(k) is v for k,v in WANT.items())
    print('CAMDUAL_PREFS_SET', sorted(changed), 'were', was, 'mode %o uid %d gid %d'%(mode,uid,gid), '(as it was: %s)'%('com.apple.camera.plist.pre-camdual' if had else 'absent, noted'))
print('CAMDUAL_MARKERS', sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-camera')))
print('CC_JOB_ADDED camera: dual-wide device in D22/AVCaptureSession.plist, Camera preferences for an iPhone 11 this capture device is not')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
