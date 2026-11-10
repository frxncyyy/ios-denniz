set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# gpu/video stage 0 (lead, 11 Nov 2026): the VideoToolbox encoder plug-in, the probe and its launchd job.
#   /System/Library/VideoEncoders/Slim.videoencoder   stock VideoToolbox dlopens this path and calls SLIMEncoderRegister (PLAN.md 1.3)
#   /usr/libexec/n2vt_probe                            the probe (runs only while /private/var/db/nord2-video-probe exists)
#   job com.nord2.videoprobe in /System/Library/xpc/launchd.plist
#   /private/var/db/nord2-video/ (1777) with install.json = what this script put where (the undo script goes by it)
# Installing again with other files (a newer plug-in or probe) replaces exactly the files install.json says are ours.
import hashlib, json, os, plistlib
R='/work/root'; S='/src'
plan=json.load(open(S+'/plan.json'))
FILES={'/System/Library/VideoEncoders/Slim.videoencoder': ('Slim.videoencoder', 0o755, '/System/Library/VideoEncoders/AppleVideoEncoder.bundle/AppleVideoEncoder'),
       '/usr/libexec/n2vt_probe': ('n2vt_probe', 0o755, '/usr/libexec/keybagd')}
DB='/private/var/db/nord2-video'; GATE='/private/var/db/nord2-video-probe'; REC=DB+'/install.json'
LP='/System/Library/xpc/launchd.plist'; JOB='/System/Library/LaunchDaemons/com.nord2.videoprobe.plist'; MR='/private/var/db/nord2-metal/receipt.json'
sha=lambda b: hashlib.sha256(b).hexdigest()
rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
new={p: open(S+'/'+v[0],'rb').read() for p,v in FILES.items()}
for p,v in FILES.items(): assert sha(new[p])==plan['sha256'][v[0]], 'payload %s is not the file of plan.json'%v[0]
job=plistlib.loads(open(S+'/com.nord2.videoprobe.plist','rb').read())
assert job['Label']=='com.nord2.videoprobe' and job['ProgramArguments']==['/usr/libexec/n2vt_probe'] and 'KeepAlive' not in job
old=json.load(open(R+REC)) if os.path.exists(R+REC) else {'files':{}}
for p,v in FILES.items():                 # nothing of anybody else's is ever replaced
    assert os.path.isdir(R+os.path.dirname(p)), 'no directory for '+p
    if os.path.lexists(R+p):
        assert os.path.isfile(R+p) and not os.path.islink(R+p) and old['files'].get(p)==sha(rd(p)), '%s exists and is not ours'%p
    assert os.path.exists(R+v[2]), 'no model for owner/group: '+v[2]
# 1. the directory and the gate
if not os.path.isdir(R+DB): os.mkdir(R+DB)
os.chmod(R+DB,0o1777); os.chown(R+DB,0,0)
# 2. the files
done={}
for p,v in FILES.items():
    st=os.stat(R+v[2]); was=sha(rd(p))[:12] if os.path.exists(R+p) else 'absent'
    put(p,new[p],v[1],st.st_uid,st.st_gid); done[p]=sha(new[p])
    print('VIDEO_INSTALLED',p,was,'->',done[p][:12],'mode %o'%(os.stat(R+p).st_mode&0o7777),'uid:gid %d:%d'%(st.st_uid,st.st_gid),len(new[p]),'bytes')
# 3. the launchd job (and the Metal updater's check: receipt.json's installed_launchd_sha256 must equal the cache file)
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw); before=len(cfg['LaunchDaemons'])
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
if cfg['LaunchDaemons'].get(JOB)==job:
    print('CC_JOB_ADDED com.nord2.videoprobe already there (%d jobs)'%before); print('CC_RECEIPT_UPDATED unchanged')
    lsha=sha(raw)
else:
    assert JOB not in cfg['LaunchDaemons'] or cfg['LaunchDaemons'][JOB].get('Label')=='com.nord2.videoprobe'
    assert not any(j.get('Label')=='com.nord2.videoprobe' for k,j in cfg['LaunchDaemons'].items() if k!=JOB and isinstance(j,dict))
    cfg['LaunchDaemons'][JOB]=job
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); st=os.stat(R+LP)
    put(LP,out,st.st_mode&0o7777,st.st_uid,st.st_gid)
    assert plistlib.loads(rd(LP))==cfg
    print('CC_JOB_ADDED com.nord2.videoprobe',before,'->',len(cfg['LaunchDaemons']),'format','bin' if fmt==plistlib.FMT_BINARY else 'xml')
    rc['installed_launchd_sha256']=sha(out); st=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),st.st_mode&0o7777,st.st_uid,st.st_gid)
    print('CC_RECEIPT_UPDATED',oldh[:12],'->',rc['installed_launchd_sha256'][:12]); lsha=sha(out)
# 4. our own record, then the gate (last: the probe runs only when everything above is in place)
put(REC,(json.dumps({'version':1,'tag':plan.get('tag'),'files':done,'job':JOB,'launchd_sha256_after':lsha},indent=1)+'\n').encode(),0o644,0,0)
if plan.get('probe',True):
    put(GATE,b'run\n',0o644,0,0); print('VIDEO_GATE on',GATE)
elif os.path.exists(R+GATE):
    os.remove(R+GATE); print('VIDEO_GATE removed')
for n in plan.get('switches_on',[]):
    assert n.startswith('nord2-video-') and '/' not in n; put('/private/var/db/'+n,b'',0o644,0,0); print('VIDEO_SWITCH on',n)
for n in plan.get('switches_off',[]):
    assert n.startswith('nord2-video-') and '/' not in n
    if os.path.exists(R+'/private/var/db/'+n): os.remove(R+'/private/var/db/'+n); print('VIDEO_SWITCH off',n)
if plan.get('tcc'):       # camera and microphone for the probe's headless capture test (rows as apps-20261104/guest-tcc.sh writes them)
    import sqlite3, time
    db=R+'/private/var/mobile/Library/TCC/TCC.db'; st=os.stat(db)
    c=sqlite3.connect(db); cur=c.cursor(); now=int(time.time())
    # (run q10ig-video4: the path row alone is answered "denied". iOS tccd names a client by its code-signing identifier; the
    #  probe's ad-hoc signature carries "n2vt_probe" -- a row of type 0 for that name, beside the path row)
    rows=[(svc,cl,ct,2,2,1,None,None,0,'UNUSED',None,0,now) for svc in ('kTCCServiceCamera','kTCCServiceMicrophone')
          for cl,ct in (('/usr/libexec/n2vt_probe',1),('n2vt_probe',0))]
    cur.executemany("INSERT OR REPLACE INTO access (service,client,client_type,auth_value,auth_reason,auth_version,csreq,policy_id,indirect_object_identifier_type,indirect_object_identifier,indirect_object_code_identity,flags,last_modified) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",rows)
    c.commit(); cur.execute("PRAGMA wal_checkpoint(TRUNCATE)"); c.commit()
    got=cur.execute("SELECT service,client_type,auth_value FROM access WHERE client='/usr/libexec/n2vt_probe' ORDER BY service").fetchall()
    print('VIDEO_TCC_ALL',cur.execute("SELECT service,client,client_type,auth_value FROM access WHERE service IN ('kTCCServiceCamera','kTCCServiceMicrophone') ORDER BY client,service").fetchall())
    c.close()
    os.chown(db,st.st_uid,st.st_gid); os.chmod(db,st.st_mode&0o7777)
    for ext in ('-wal','-shm'):
        if os.path.exists(db+ext): os.chown(db+ext,st.st_uid,st.st_gid); os.chmod(db+ext,st.st_mode&0o7777)
    assert got==[('kTCCServiceCamera',1,2),('kTCCServiceMicrophone',1,2)], got
    print('VIDEO_TCC',got)
if plan.get('camera_h264'):   # Camera.app: "Most Compatible" = H.264 video and JPEG photos (CameraUI 16.7 reads this key with
    # integerForKey: in -[CAMUserPreferences readPreferencesWithOverrides:...] @0x1ceabc91c: 1 -> _preferHEVCWhenAvailable = NO;
    # the second key keeps the one-time "upgrade to High Efficiency" at 0x1ceabc85c from undoing it). Method of guest-camback.sh.
    PREFS=R+'/private/var/mobile/Library/Preferences/com.apple.camera.plist'; KEEP=PREFS+'.pre-video'
    WANT={'CAMUserPreferenceCaptureEncodingBehavior': 1, 'CAMUserPreferenceDidPerformSpatialOverCaptureHighEfficiencyUpgrade': True}
    if not os.path.isfile(PREFS):
        print('VIDEO_CAMPREFS no com.apple.camera.plist: nothing changed')
    else:
        pst=os.stat(PREFS); prefs=plistlib.loads(open(PREFS,'rb').read()); assert isinstance(prefs, dict)
        was={k: prefs[k] for k in WANT if k in prefs}
        if all(k in prefs and prefs[k]==v and type(prefs[k]) is type(v) for k,v in WANT.items()):
            print('VIDEO_CAMPREFS already', was)
        else:
            def putp(path, data):
                tmp=path+'.nord2tmp'
                with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
                os.chmod(tmp,pst.st_mode&0o7777); os.chown(tmp,pst.st_uid,pst.st_gid); os.replace(tmp,path)
            if not os.path.exists(KEEP): putp(KEEP, plistlib.dumps({'was': was, 'absent': sorted(k for k in WANT if k not in prefs)}, fmt=plistlib.FMT_BINARY))
            prefs.update(WANT); putp(PREFS, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY))
            assert all(plistlib.loads(open(PREFS,'rb').read()).get(k)==v for k,v in WANT.items())
            print('VIDEO_CAMPREFS set', WANT, 'were', was)
print('VIDEO_STATE',sorted(os.listdir(R+DB)),[n for n in sorted(os.listdir(R+'/private/var/db')) if n.startswith('nord2-video')],sorted(os.listdir(R+'/System/Library/VideoEncoders')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
