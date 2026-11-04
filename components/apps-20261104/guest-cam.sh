set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import plistlib, json, hashlib, os
R='/work/root'
# 1. undo guest-nocap.sh: mediaserverd serves the camera's mach services again (the kernel now has /dev/redwood-camera and the
#    RedwoodAudio bundle makes the capture device: gpu/build/camera-userland-20261109)
LP=R+'/System/Library/xpc/launchd.plist'
BK=R+'/private/var/db/nord2-metal/mediaserverd-machservices.pre-nocap.plist'
WANT={'com.apple.coremedia.capturesource','com.apple.coremedia.capturesession','com.apple.coremedia.cameraviewfinder'}
raw=open(LP,'rb').read()
fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw)
job=cfg['LaunchDaemons']['/System/Library/LaunchDaemons/com.apple.mediaserverd.plist']
ms=job['MachServices']
back=plistlib.loads(open(BK,'rb').read())
assert set(back)==WANT, 'unexpected backup %r'%sorted(back)
back_up=[k for k in sorted(WANT) if k not in ms]
for k in back_up: ms[k]=back[k]
# 2. com.apple.tccd: Camera.app gets its camera permission from it (without it the capture graph never asks for a stream). It is a stock job of this
#    launchd.plist (launched on demand by its mach service); check, do not change.
tccd=cfg['LaunchDaemons'].get('/System/Library/LaunchDaemons/com.apple.tccd.plist')
assert tccd and tccd['Label']=='com.apple.tccd' and 'com.apple.tccd' in tccd['MachServices'], 'tccd job missing from launchd.plist'
assert os.path.exists(R+'/System/Library/PrivateFrameworks/TCC.framework/Support/tccd'), 'tccd binary missing'
new=plistlib.dumps(cfg,fmt=fmt)
if back_up:
    tmp=LP+'.nord2tmp'; open(tmp,'wb').write(new); os.chmod(tmp,os.stat(LP).st_mode&0o7777); os.chown(tmp,os.stat(LP).st_uid,os.stat(LP).st_gid); os.replace(tmp,LP)
    assert plistlib.loads(open(LP,'rb').read())==cfg
    RP=R+'/private/var/db/nord2-metal/receipt.json'
    rc=json.load(open(RP)); old=rc['installed_launchd_sha256']
    assert old==hashlib.sha256(raw).hexdigest(), 'receipt does not match launchd.plist'
    rc['installed_launchd_sha256']=hashlib.sha256(new).hexdigest()
    open(RP+'.tmp','w').write(json.dumps(rc,indent=2)+'\n'); os.replace(RP+'.tmp',RP)
    print('CAM_MACHSERVICES_RESTORED', back_up, old[:12], '->', rc['installed_launchd_sha256'][:12])
else:
    print('CAM_MACHSERVICES_ALREADY_THERE')
print('CAM_TCCD_JOB_OK', tccd['ProgramArguments'])
# 3. the model's camera plists (hw.target D22AP -> CMCapture.framework/D22): the three-camera versions made by tools/make_camera_plists.py
D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
STOCK={'AVCaptureSession.plist':'f240baa42e3b72e9f76b4ceff1b813fd64ed136a5463f6523db1d404520c5343','CameraSetup.plist':'c009561b618812be105a037da4d1b545724ba73d93f4310b53945f4cbfd21ec4'}
for n,stock in STOCK.items():
    cur=open(D+'/'+n,'rb').read(); src=open('/src/D22/'+n,'rb').read()
    ch=hashlib.sha256(cur).hexdigest(); sh=hashlib.sha256(src).hexdigest()
    if ch==sh: print('CAM_PLIST_ALREADY', n); continue
    assert ch==stock, '%s is neither the iPhone X stock file nor ours: %s'%(n,ch)
    st=os.stat(D+'/'+n)
    if not os.path.exists(D+'/'+n+'.pre-cam'): open(D+'/'+n+'.pre-cam','wb').write(cur)
    tmp=D+'/'+n+'.nord2tmp'; open(tmp,'wb').write(src); os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,D+'/'+n)
    assert hashlib.sha256(open(D+'/'+n,'rb').read()).hexdigest()==sh
    print('CAM_PLIST_INSTALLED', n, sh[:12])
# 4. StartRecorderServer stays FALSE in MediaToolbox.framework/*/SubServerConfiguration.plist (sweet-root-tweaks.py audio()). With false the stock
#    FigMediaServerStart starts no capture server and nobody serves com.apple.coremedia.capturesource (Camera hangs in its handshake: run q10gk);
#    with true the stock start runs BEFORE RedwoodAudio.driver replaces the device vendor and CMCapture keeps an empty source list for good (black
#    preview, no lens: run q10gl). So RedwoodAudio.driver (rwcam_init.c) calls FigStartCaptureServers itself after its hooks, and only when this
#    flag is false. Here: put back false where run q10gl's version of this script set true, and drop its *.pre-cam copies.
import glob
MT=R+'/System/Library/Frameworks/MediaToolbox.framework'
confs=sorted(glob.glob(MT+'/*/SubServerConfiguration.plist'))
assert any(c.endswith('/D22/SubServerConfiguration.plist') for c in confs), 'no D22 SubServerConfiguration.plist: %r'%confs
for c in confs:
    cur=open(c,'rb').read(); d=plistlib.loads(cur)
    rel=os.path.relpath(c,R)
    assert cur[:6]!=b'bplist', '%s is a binary plist: rwcam_init.c reads it as XML'%rel
    if os.path.exists(c+'.pre-cam'): os.unlink(c+'.pre-cam')
    if d.get('StartRecorderServer') is False: print('CAM_RECORDER_SERVER_ALREADY_OFF', rel); continue
    st=os.stat(c)
    d['StartRecorderServer']=False
    tmp=c+'.nord2tmp'; open(tmp,'wb').write(plistlib.dumps(d,fmt=plistlib.FMT_XML)); os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,c)
    assert plistlib.loads(open(c,'rb').read())==d
    print('CAM_RECORDER_SERVER_OFF', rel)
print('CC_JOB_ADDED camera mach services + D22 plists; StartRecorderServer false (the bundle starts the capture servers)')
print('CC_RECEIPT_UPDATED' if back_up else 'CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
