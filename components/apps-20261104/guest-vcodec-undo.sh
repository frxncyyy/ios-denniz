set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# Undo of guest-vcodec.sh (opus-ios): removes exactly what install.json says is ours (each file only if it still has our hash),
# the three launchd jobs and the gates. Results are kept: /private/var/db/nord2-vcodec/{out/,*.log,selftest-*.txt}.
# The vendor directories are removed only when empty (they hold nothing but our five files).
import hashlib, json, os, plistlib
R='/work/root'; DB='/private/var/db/nord2-vcodec'; REC=DB+'/install.json'; VD='/usr/local/lib/n2vendor'
LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
ALL=('com.nord2.vcodec-selftest','com.nord2.vcodec-probe','com.nord2.vcodecd','com.nord2.vcodec-dprobe','com.nord2.jpeg-selftest'); KEY=lambda l: '/System/Library/LaunchDaemons/'+l+'.plist'
sha=lambda b: hashlib.sha256(b).hexdigest()
rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
if not os.path.exists(R+REC):
    print('VCODEC_UNDO nothing installed (no install.json)')
else:
    rec=json.load(open(R+REC))
    for p,h in rec['files'].items():
        assert p.startswith('/usr/local/libexec/n2vcd_') or p in ('/usr/local/libexec/vcodecd','/usr/local/libexec/n2jpeg_phone') or p.startswith(VD+'/'), p
        if os.path.exists(R+p):
            assert sha(rd(p))==h, '%s is not the file we installed: left alone, undo stopped'%p
            os.remove(R+p); print('VCODEC_REMOVED',p)
    # opus-lead2: VideoToolbox plug-ins: put back the previous file (gpu/video's) or remove ours; fix gpu/video's record
    VREC='/private/var/db/nord2-video/install.json'; PREV=DB+'/prev'
    vrec=json.load(open(R+VREC)) if os.path.exists(R+VREC) else None
    for p,h in rec.get('plugins',{}).items():
        assert p in ('/System/Library/VideoEncoders/Slim.videoencoder','/System/Library/VideoDecoders/Slim.videodecoder'), p
        before=rec.get('plugins_prev',{}).get(p); keep=PREV+'/'+os.path.basename(p)
        if os.path.exists(R+p):
            assert sha(rd(p))==h, '%s is not the plug-in we installed: left alone, undo stopped'%p
        if before:
            b=rd(keep); assert sha(b)==before, 'kept previous %s has the wrong hash'%keep
            st=os.stat(R+p) if os.path.exists(R+p) else os.stat(R+'/System/Library/VideoEncoders/AppleVideoEncoder.bundle/AppleVideoEncoder')
            put(p,b,0o755,st.st_uid,st.st_gid); os.remove(R+keep); print('VCODEC_RESTORED',p,'->',before[:12])
            if vrec is not None and vrec['files'].get(p)==h:
                vrec['files'][p]=before; vst=os.stat(R+VREC)
                put(VREC,(json.dumps(vrec,indent=1)+'\n').encode(),vst.st_mode&0o7777,vst.st_uid,vst.st_gid)
                print('VCODEC_RESTORED gpu/video install.json names',before[:12],'for',p)
        elif os.path.exists(R+p):
            os.remove(R+p); print('VCODEC_REMOVED',p)
    if os.path.isdir(R+PREV) and not os.listdir(R+PREV): os.rmdir(R+PREV); print('VCODEC_REMOVED (empty dir)',PREV)
    for n in rec.get('video_switches_created',[]):
        g='/private/var/db/nord2-camera-keephevc' if n=='camera-keephevc' else '/private/var/db/nord2-video-'+n
        if os.path.isfile(R+g): os.remove(R+g); print('VCODEC_REMOVED',g)
    os.remove(R+REC)
for d in (VD+'/bin',VD+'/lib',VD+'/streams',VD+'/jpeg',VD,'/usr/local/lib'):
    if os.path.isdir(R+d) and not os.listdir(R+d): os.rmdir(R+d); print('VCODEC_REMOVED (empty dir)',d)
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; cfg=plistlib.loads(raw)
ours=[KEY(l) for l in ALL if KEY(l) in cfg['LaunchDaemons']]
if ours:
    rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
    assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
    before=len(cfg['LaunchDaemons'])
    for k in ours: del cfg['LaunchDaemons'][k]
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); st=os.stat(R+LP); put(LP,out,st.st_mode&0o7777,st.st_uid,st.st_gid)
    assert plistlib.loads(rd(LP))==cfg
    print('CC_JOB_ADDED (removed)',ours,before,'->',len(cfg['LaunchDaemons']))
    rc['installed_launchd_sha256']=sha(out); st=os.stat(R+MR); put(MR,(json.dumps(rc,indent=2)+'\n').encode(),st.st_mode&0o7777,st.st_uid,st.st_gid)
    print('CC_RECEIPT_UPDATED',oldh[:12],'->',rc['installed_launchd_sha256'][:12])
else:
    print('CC_JOB_ADDED (removed) jobs were not there'); print('CC_RECEIPT_UPDATED unchanged')
for n in ('run','selftest','probe','dprobe','jpeg'):
    g='/private/var/db/nord2-vcodec-'+n
    if os.path.isfile(R+g): os.remove(R+g); print('VCODEC_REMOVED',g)
print('VCODEC_STATE',sorted(os.listdir(R+DB)) if os.path.isdir(R+DB) else None)
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
