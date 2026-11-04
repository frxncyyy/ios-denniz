set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-vcodec-camzc.sh = guest-vcodec.sh (unchanged) + step 5 (camera switches /private/var/db/nord2-camera-{gpu-isp,zerocopy}: q10jn). TEST ONLY.
# gpu/hwcodec (opus-ios, 11 Nov 2026): hardware video encoder host. Payload: stage-vcodec.sh. Undo: guest-vcodec-undo.sh.
#   /usr/local/libexec/n2vcd_selftest   process facilities + vendor bring-up with fake hardware (no kernel driver needed)
#   /usr/local/libexec/n2vcd_probe      boot-time test client of vcodecd (relay /dev/redwood-vcodec-client)
#   /usr/local/libexec/vcodecd          the daemon (minor 0 of /dev/redwood-vcodec); only when the payload has it
#   /usr/local/lib/n2vendor/{bin,lib}/  the phone's own five vendor objects (user's files: never committed)
#   jobs com.nord2.vcodec-selftest / com.nord2.vcodecd / com.nord2.vcodec-probe in /System/Library/xpc/launchd.plist, each a
#   no-op unless its gate /private/var/db/nord2-vcodec-{selftest,run,probe} exists; selftest and probe remove their gate when
#   they start a run (one run per installation), vcodecd keeps it.
#   /private/var/db/nord2-vcodec/ (0755): install.json (what this script put where), logs, out/ (bit streams), selftest-*.txt
# Installing again replaces exactly the files install.json says are ours; nothing of anybody else's is ever replaced.
# opus-decint (11 Nov 2026): optional hardware DECODE test (plan['decoder']): /usr/local/libexec/n2vcd_dprobe, the test streams
#   in /usr/local/lib/n2vendor/streams/, vcodecd started with --decoder --guest-log 300, and the job com.nord2.vcodec-dprobe
#   (gate /private/var/db/nord2-vcodec-dprobe, one-shot) that waits for the encode probe's DONE, decodes every stream
#   through the relay and writes out/dec-*.yuv and dprobe.log (fsync per line).
# opus-jpeg (12 Nov 2026): optional hardware JPEG test (plan['jpeg']): /usr/local/libexec/n2jpeg_phone, decode inputs in
#   /usr/local/lib/n2vendor/jpeg/, job com.nord2.jpeg-selftest (gate /private/var/db/nord2-vcodec-jpeg, one-shot): encodes the
#   kernel test picture through /dev/redwood-jpeg (640x480..4032x3024), writes jpeg-selftest.log (fsync per line), out/jpeg-*.jpg and,
#   once the kernel decodes, out/jdec-*.yuv.  Needs no vcodecd.
# opus-cam (12 Nov 2026): plan['prod'] = the production payload of stage-vcodec.sh --prod: only vcodecd (job com.nord2.vcodecd,
#   RunAtLoad + KeepAlive {SuccessfulExit: false}, gate 'run' kept), no selftest/probe files or jobs (a previous test install's
#   are removed: files by install.json hash, jobs by label), one-shot gates removed, video switches as planned.
# opus-lead2: optional VideoToolbox plug-ins in the same transaction (plan['plugins']):
#   /System/Library/VideoEncoders/Slim.videoencoder  (gpu/video's plug-in + P04; replaces gpu/video's copy, which is accepted
#       as the previous owner by its own install.json hash; that record is updated to the new hash so guest-video*.sh keep
#       working, and the previous file is kept in /private/var/db/nord2-vcodec/prev/ for guest-vcodec-undo.sh)
#   /System/Library/VideoDecoders/Slim.videodecoder  (DEC06; new file)
#   switch files /private/var/db/nord2-video-<name> on/off (plan['video_switches_on'/'_off']); default hardware VT paths OFF.
import hashlib, json, os, plistlib
R='/work/root'; S='/src'
plan=json.load(open(S+'/plan.json'))
LX='/usr/local/libexec'; VD='/usr/local/lib/n2vendor'; DB='/private/var/db/nord2-vcodec'; REC=DB+'/install.json'
GATE=lambda n: '/private/var/db/nord2-vcodec-'+n
SWP=lambda n: '/private/var/db/nord2-camera-keephevc' if n=='camera-keephevc' else '/private/var/db/nord2-video-'+n   # opus-jpeg: + trace, probe (gpu/video's n2vt_probe gate), camera-keephevc
LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
MODEL='/usr/libexec/keybagd'                      # owner/group model (root:wheel), as guest-video.sh
sha=lambda b: hashlib.sha256(b).hexdigest()
rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
def mkdir(path, mode):
    made=not os.path.isdir(R+path)
    if made: os.mkdir(R+path)
    assert os.path.isdir(R+path) and not os.path.islink(R+path), path
    os.chmod(R+path,mode); os.chown(R+path,0,0)
    return made
# ---- what goes where, checked against plan.json before anything is written
FILES={}
for n,h in plan['files'].items():
    assert n in ('n2vcd_selftest','n2vcd_probe','n2vcd_dprobe','vcodecd','n2jpeg_phone'), n
    FILES[LX+'/'+n]=(S+'/'+n, h, 0o755)
DEC=bool(plan.get('decoder'))
for n,h in plan.get('streams',{}).items():
    assert DEC and '/' not in n and n[:1] != '.' and n.rsplit('.',1)[-1] in ('h264','h265','ivf','m2v','m4v','h263'), n   # opus-fmt: + other decoder formats
    FILES[VD+'/streams/'+n]=(S+'/streams/'+n, h, 0o644)
JPG=bool(plan.get('jpeg'))
for n,h in plan.get('jpeg_inputs',{}).items():
    assert JPG and '/' not in n and n[:1] != '.' and n.endswith('.jpg'), n
    FILES[VD+'/jpeg/'+n]=(S+'/jpeg/'+n, h, 0o644)
assert JPG == ('n2jpeg_phone' in plan['files']), 'jpeg payload incomplete'
assert not DEC or ('n2vcd_dprobe' in plan['files'] and 'vcodecd' in plan['files'] and plan.get('streams')), 'decoder payload incomplete'
for r,h in plan['vendor'].items():
    assert r in ('bin/vpud','lib/libvpud_vcodec.so','lib/libvcodecdrv.so','lib/libvcodec_utility.so','lib/libvcodec_oal.so'), r
    FILES[VD+'/'+r]=(S+'/vendor/'+r, h, 0o644)    # data for the interpreter, never executed by iOS
PROD=bool(plan.get('prod'))                       # opus-cam: production payload = vcodecd only, no test jobs
assert len(plan['vendor'])==5 and (('vcodecd' in plan['files'] and not DEC) if PROD else
                                   ('n2vcd_selftest' in plan['files'] and 'n2vcd_probe' in plan['files']))
new={p: open(v[0],'rb').read() for p,v in FILES.items()}
for p,v in FILES.items(): assert sha(new[p])==v[1], 'payload %s is not the file of plan.json'%v[0]
old=json.load(open(R+REC)) if os.path.exists(R+REC) else {'files':{},'dirs':[]}
for p in FILES:
    if os.path.lexists(R+p):
        assert os.path.isfile(R+p) and not os.path.islink(R+p) and old['files'].get(p)==sha(rd(p)), '%s exists and is not ours'%p
# ---- VideoToolbox plug-ins: checked here too, before anything is written
PLUGDST={'Slim.videoencoder':'/System/Library/VideoEncoders/Slim.videoencoder','Slim.videodecoder':'/System/Library/VideoDecoders/Slim.videodecoder'}
PLUGMODEL='/System/Library/VideoEncoders/AppleVideoEncoder.bundle/AppleVideoEncoder'
VREC='/private/var/db/nord2-video/install.json'; PREV=DB+'/prev'
vrec=json.load(open(R+VREC)) if os.path.exists(R+VREC) else None
PLUG={}; prev=dict(old.get('plugins_prev',{}))
for n,h in plan.get('plugins',{}).items():
    p=PLUGDST[n]; data=open(S+'/'+n,'rb').read(); assert sha(data)==h, 'payload %s is not the file of plan.json'%n
    assert os.path.isdir(R+os.path.dirname(p)), 'no directory for '+p
    PLUG[p]=data
    if os.path.lexists(R+p):
        assert os.path.isfile(R+p) and not os.path.islink(R+p), p
        cur=sha(rd(p))
        ours=old.get('plugins',{}).get(p)==cur
        video=vrec is not None and vrec['files'].get(p)==cur
        assert ours or video, '%s exists and is neither ours nor gpu/video\'s (install.json): not touching it'%p
        if not ours: prev[p]=cur                    # first replacement: keep the previous file for the undo
    elif p not in old.get('plugins',{}):
        prev[p]=None                                # absent before us: the undo removes it
for n in plan.get('video_switches_on',[])+plan.get('video_switches_off',[]):
    assert n in ('claim-hw','hwdec','hwdec-no-hevc','hwenc-sync','off','no-avc','no-hevc','no-jpeg','quiet','trace','probe','camera-keephevc'), n
if PLUG: assert os.path.exists(R+PLUGMODEL), 'no owner model '+PLUGMODEL
assert os.path.isdir(R+LX) and os.path.exists(R+MODEL)
st=os.stat(R+MODEL); assert (st.st_uid,st.st_gid)==(0,0), 'model owner %d:%d'%(st.st_uid,st.st_gid)
# ---- launchd jobs (built here from plan.json, so the guest is the single source of truth)
DAEMON='vcodecd' in plan['files']
JOBS={}
COMMON={'UserName':'root','RunAtLoad':True,'ProcessType':'Interactive','EnablePressuredExit':False,
        'JetsamProperties':{'JetsamPriority':140,'JetsamMemoryLimit':3072}}   # opus-guestmem: vc3 per-process-limit kill; the daemons also raise their own limit (include/n2memlimit.h)
if not PROD:
  JOBS['com.nord2.vcodec-selftest']=dict(COMMON, Label='com.nord2.vcodec-selftest',
    ProgramArguments=[LX+'/n2vcd_selftest','--gate',GATE('selftest'),'--vendor',VD,'--out',DB],
    StandardOutPath=DB+'/selftest.log', StandardErrorPath=DB+'/selftest.log')
  JOBS['com.nord2.vcodec-probe']=dict(COMMON, Label='com.nord2.vcodec-probe',
    ProgramArguments=[LX+'/n2vcd_probe','--gate',GATE('probe'),'--out',DB+'/out']+[str(a) for a in plan['probe_args']],
    StandardOutPath=DB+'/probe.log', StandardErrorPath=DB+'/probe.log')
if DEC:
    JOBS['com.nord2.vcodec-dprobe']=dict(COMMON, Label='com.nord2.vcodec-dprobe',
        ProgramArguments=[LX+'/n2vcd_dprobe','--gate',GATE('dprobe'),'--out',DB+'/out','--log',DB+'/dprobe.log']+
                         [a for n in plan['stream_order'] if n in plan['streams'] for a in ('--in',VD+'/streams/'+n)]+   # a name may repeat (A/B)
                         [str(a) for a in plan['dprobe_args']],
        StandardOutPath=DB+'/dprobe.stdout.log', StandardErrorPath=DB+'/dprobe.stdout.log')
if JPG:
    JOBS['com.nord2.jpeg-selftest']=dict(COMMON, Label='com.nord2.jpeg-selftest',
        ProgramArguments=[LX+'/n2jpeg_phone','run','--gate',GATE('jpeg'),'--out',DB+'/out','--log',DB+'/jpeg-selftest.log']+
                         [a for n in plan['jpeg_order'] if n in plan['jpeg_inputs'] for a in ('--dec',VD+'/jpeg/'+n)]+
                         [str(a) for a in plan.get('jpeg_args',[])],
        StandardOutPath=DB+'/jpeg-stdout.log', StandardErrorPath=DB+'/jpeg-stdout.log')
if DAEMON:
    JOBS['com.nord2.vcodecd']=dict(COMMON, Label='com.nord2.vcodecd',
        ProgramArguments=[LX+'/vcodecd','--gate',GATE('run'),'--vendor',VD,'--hw-timeout-ms','6000']+
                         (['--decoder','--guest-log','300'] if DEC else [])+
                         (['--restart-on-switch'] if plan.get('restart_on_switch') else [])+   # opus-enc4k: exit 75 on a codec switch, KeepAlive restarts
                         [str(x) for x in plan.get('vcodecd_extra', [])],   # opus-enc4k: stage-vcodec.sh --vcodecd-arg (e.g. --venc-keep-power)   # opus-lead: 3 s per IPI half (default 2000 = 1 s) for the interpreted vendor INIT/first IDR on the phone
        EnvironmentVariables={'N2HW_VENDOR_DIR':VD}, ThrottleInterval=10,
        KeepAlive={'SuccessfulExit':False} if plan.get('keepalive') else False,
        StandardOutPath=DB+'/vcodecd.log', StandardErrorPath=DB+'/vcodecd.log')
KEY=lambda label: '/System/Library/LaunchDaemons/'+label+'.plist'
ALL=('com.nord2.vcodec-selftest','com.nord2.vcodec-probe','com.nord2.vcodecd','com.nord2.vcodec-dprobe','com.nord2.jpeg-selftest')
# ---- 1. directories
for d,m in ((DB,0o755),(DB+'/out',0o755),('/usr/local/lib',0o755),(VD,0o755),(VD+'/bin',0o755),(VD+'/lib',0o755))+(((VD+'/streams',0o755),) if DEC else ())+(((VD+'/jpeg',0o755),) if JPG else ()):
    if mkdir(d,m): print('VCODEC_DIR created',d,'mode %o'%m)
# ---- 2. files
done={}
for p,v in sorted(FILES.items()):
    was=sha(rd(p))[:12] if os.path.exists(R+p) else 'absent'
    put(p,new[p],v[2],0,0); done[p]=v[1]
    print('VCODEC_INSTALLED',p,was,'->',v[1][:12],'mode %o'%(os.stat(R+p).st_mode&0o7777),len(new[p]),'bytes')
for p in old['files']:                            # a file of the previous install that this payload no longer has (e.g. vcodecd)
    if p not in FILES and os.path.isfile(R+p) and sha(rd(p))==old['files'][p]:
        os.remove(R+p); print('VCODEC_REMOVED',p,'(not in this payload)')
# ---- 2b. VideoToolbox plug-ins (keep the previous gpu/video file first, then replace, then fix gpu/video's record)
plugdone={}
if PLUG and mkdir(PREV,0o755): print('VCODEC_DIR created',PREV)
for p,data in sorted(PLUG.items()):
    keep=PREV+'/'+os.path.basename(p)
    if prev.get(p) and not os.path.exists(R+keep):
        b=rd(p); assert sha(b)==prev[p]; put(keep,b,0o644,0,0); print('VCODEC_PLUGIN kept previous',p,prev[p][:12],'in',keep)
    st=os.stat(R+PLUGMODEL); was=sha(rd(p))[:12] if os.path.exists(R+p) else 'absent'
    put(p,data,0o755,st.st_uid,st.st_gid); plugdone[p]=sha(data)
    print('VCODEC_PLUGIN',p,was,'->',plugdone[p][:12],'mode %o'%(os.stat(R+p).st_mode&0o7777),len(data),'bytes')
    if vrec is not None and p in vrec['files'] and vrec['files'][p]!=plugdone[p]:
        vrec['files'][p]=plugdone[p]; vst=os.stat(R+VREC)
        put(VREC,(json.dumps(vrec,indent=1)+'\n').encode(),vst.st_mode&0o7777,vst.st_uid,vst.st_gid)
        print('VCODEC_PLUGIN gpu/video install.json now names',plugdone[p][:12],'for',p)
for p in old.get('plugins',{}):                  # a plug-in of the previous install that this payload no longer has
    if p not in PLUG: plugdone[p]=old['plugins'][p]; print('VCODEC_PLUGIN kept (not in this payload, undo restores it)',p)
# ---- 3. the launchd jobs (and the Metal updater's check: receipt.json's installed_launchd_sha256 must equal the cache file)
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']; before=len(LD)
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
for label in ALL:                                  # nobody else uses our labels
    assert not any(isinstance(j,dict) and j.get('Label')==label for k,j in LD.items() if k!=KEY(label)), label
    assert KEY(label) not in LD or LD[KEY(label)].get('Label')==label
want={KEY(l): JOBS[l] for l in JOBS}
changed=False
for label in ALL:
    k=KEY(label)
    if k in want and LD.get(k)!=want[k]: LD[k]=want[k]; changed=True; print('VCODEC_JOB set',label,json.dumps(want[k]['ProgramArguments']))
    elif k not in want and k in LD: del LD[k]; changed=True; print('VCODEC_JOB removed',label)
if not changed:
    print('CC_JOB_ADDED vcodec jobs already there (%d jobs)'%before); print('CC_RECEIPT_UPDATED unchanged'); lsha=sha(raw)
else:
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid)
    assert plistlib.loads(rd(LP))==cfg
    print('CC_JOB_ADDED',sorted(JOBS),before,'->',len(cfg['LaunchDaemons']),'format','bin' if fmt==plistlib.FMT_BINARY else 'xml')
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('CC_RECEIPT_UPDATED',oldh[:12],'->',rc['installed_launchd_sha256'][:12]); lsha=sha(out)
# ---- 4. our record, then the gates (last: a job runs only when everything above is in place)
swc=list(old.get('video_switches_created',[]))
for n in plan.get('video_switches_off',[]):
    g=SWP(n)
    if os.path.exists(R+g): os.remove(R+g); print('VCODEC_VIDEO_SWITCH off',g)
    if n in swc: swc.remove(n)
for n in plan.get('video_switches_on',[]):
    g=SWP(n)
    if not os.path.exists(R+g): put(g,b'',0o644,0,0); swc.append(n) if n not in swc else None
    print('VCODEC_VIDEO_SWITCH on',g)
if 'trace' in plan.get('video_switches_on',[]) and os.path.isdir(R+'/private/var/db/nord2-video'):   # opus-jpeg: trace log dir, any process
    T='/private/var/db/nord2-video/trace'
    if not os.path.isdir(R+T): os.mkdir(R+T)
    os.chmod(R+T,0o1777); os.chown(R+T,0,0); print('VCODEC_TRACE_DIR',T,'mode 1777')
put(REC,(json.dumps({'version':2,'tag':plan.get('tag'),'files':done,'jobs':sorted(JOBS),'launchd_sha256_after':lsha,
    'plugins':plugdone,'plugins_prev':{p:prev[p] for p in plugdone if p in prev},'video_switches_created':sorted(set(swc))},indent=1)+'\n').encode(),0o644,0,0)
for n in plan.get('gates_off',[]):
    if os.path.exists(R+GATE(n)): os.remove(R+GATE(n)); print('VCODEC_GATE off',GATE(n))
for n in plan.get('gates_on',[]):
    assert n in ('selftest','probe') or (n=='run' and DAEMON) or (n=='dprobe' and DEC) or (n=='jpeg' and JPG), 'gate %s without its program'%n
    put(GATE(n),b'run\n',0o644,0,0); print('VCODEC_GATE on',GATE(n))
print('VCODEC_STATE',sorted(os.listdir(R+DB)),[n for n in sorted(os.listdir(R+'/private/var/db')) if n.startswith('nord2-vcodec')],
      sorted(os.listdir(R+VD+'/lib')),[n for n in sorted(os.listdir(R+LX))])
if plugdone: print('VCODEC_PLUGINS',{p:h[:12] for p,h in plugdone.items()},'switches',[n for n in sorted(os.listdir(R+'/private/var/db')) if n.startswith('nord2-video-')])
# ---- 5. (camzc, q10jn; from the gpu-isp guest) camera switch files /private/var/db/nord2-camera-<name>, plan['camera_switches_on'].
#   gpu-isp: the camera bundle (RedwoodAudio.driver, rwcam_device.c) asks libredwoodair.dylib's rwa_isp_* (GPU) for the streaming pictures
#   while it exists (read at the first stream start of mediaserverd); the first 8 pictures of each output and one every 2 s are compared with
#   the processor's and a wrong/black one turns the GPU off for that output. The file carries a marker so that guest-vcodec-camzc-undo.sh
#   removes only a switch made here.
#   zerocopy (q10jn): the readers read their frames into the GPU's memory and the viewfinder's/film's pictures are rendered there (no upload).
CDB='/private/var/db'; CMARK=b'nord2 camzc switch (guest-vcodec-camzc.sh)\n'
for n in plan.get('camera_switches_on',[]):
    assert n in ('gpu-isp', 'zerocopy'), n
    g=CDB+'/nord2-camera-'+n
    if os.path.lexists(R+g):
        assert os.path.isfile(R+g) and not os.path.islink(R+g), g
        print('CAMZC_SWITCH already there',g,len(rd(g)),'bytes')
    else:
        put(g,CMARK,0o644,0,0); print('CAMZC_SWITCH on',g)
print('CAMZC_STATE',sorted(n for n in os.listdir(R+CDB) if n.startswith('nord2-camera-')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
