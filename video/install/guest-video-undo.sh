set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# Undo of guest-video.sh: removes exactly what install.json says is ours (each file only if it still has our hash), the
# launchd job, the gate and the switches. Probe results (/private/var/db/nord2-video/probe-*.jsonl) are kept.
import hashlib, json, os, plistlib
R='/work/root'; DB='/private/var/db/nord2-video'; REC=DB+'/install.json'
LP='/System/Library/xpc/launchd.plist'; JOB='/System/Library/LaunchDaemons/com.nord2.videoprobe.plist'; MR='/private/var/db/nord2-metal/receipt.json'
sha=lambda b: hashlib.sha256(b).hexdigest()
rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
if not os.path.exists(R+REC):
    print('VIDEO_UNDO nothing installed (no install.json)')
else:
    rec=json.load(open(R+REC))
    for p,h in rec['files'].items():
        assert p in ('/System/Library/VideoEncoders/Slim.videoencoder','/usr/libexec/n2vt_probe')
        if os.path.exists(R+p):
            assert sha(rd(p))==h, '%s is not the file we installed: left alone, undo stopped'%p
            os.remove(R+p); print('VIDEO_REMOVED',p)
    os.remove(R+REC)
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; cfg=plistlib.loads(raw)
if JOB in cfg['LaunchDaemons']:
    rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
    assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
    before=len(cfg['LaunchDaemons']); del cfg['LaunchDaemons'][JOB]
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); st=os.stat(R+LP); put(LP,out,st.st_mode&0o7777,st.st_uid,st.st_gid)
    assert plistlib.loads(rd(LP))==cfg
    print('CC_JOB_ADDED (removed) com.nord2.videoprobe',before,'->',len(cfg['LaunchDaemons']))
    rc['installed_launchd_sha256']=sha(out); st=os.stat(R+MR); put(MR,(json.dumps(rc,indent=2)+'\n').encode(),st.st_mode&0o7777,st.st_uid,st.st_gid)
    print('CC_RECEIPT_UPDATED',oldh[:12],'->',rc['installed_launchd_sha256'][:12])
else:
    print('CC_JOB_ADDED (removed) job was not there'); print('CC_RECEIPT_UPDATED unchanged')
for n in sorted(os.listdir(R+'/private/var/db')):
    if n.startswith('nord2-video-') and os.path.isfile(R+'/private/var/db/'+n): os.remove(R+'/private/var/db/'+n); print('VIDEO_REMOVED /private/var/db/'+n)
print('VIDEO_STATE',sorted(os.listdir(R+DB)) if os.path.isdir(R+DB) else None,sorted(os.listdir(R+'/System/Library/VideoEncoders')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
