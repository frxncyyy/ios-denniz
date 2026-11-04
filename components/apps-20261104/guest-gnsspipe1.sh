set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# gnsspipe1: GNSS M2 phone test (byte pipe). /usr/local/libexec/n2gpsdl_pipe (gpu/hwcodec/gnss/probe) + launchd job com.nord2.gnsspipe
# (RunAtLoad, one-shot, gate /private/var/db/nord2-gnsspipe): after --delay opens /dev/gpsdl0 (kernel q10jr2: patch 0350..0352,
# boot-arg nord2gnss=1) and reads the byte pipe and prints "N2GPIPE ..." lines.
#
# launchd.plist edited like guest-vcodec.sh (Metal receipt installed_launchd_sha256 checked before, updated after).
import os, json, hashlib, plistlib
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2gpsdl_pipe'; GATE='/private/var/db/nord2-gnsspipe'; REC='/private/var/db/nord2-gnsspipe.json'
LABEL='com.nord2.gnsspipe'; KEY='/System/Library/LaunchDaemons/'+LABEL+'.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
plan=json.load(open('/src/plan.json')); exe=open('/src/n2gpsdl_pipe','rb').read()
assert sha(exe)==plan['files']['n2gpsdl_pipe'], 'n2gpsdl_pipe is not the planned binary'
assert exe[:4]==b'\xcf\xfa\xed\xfe'
args=[str(a) for a in plan['args']]
JOB={'Label':LABEL,'UserName':'root','RunAtLoad':True,'ProcessType':'Interactive','EnablePressuredExit':False,
     'ProgramArguments':[BIN,'--gate',GATE,]+args}
# ---- checks first
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
assert not any(isinstance(j,dict) and j.get('Label')==LABEL for k,j in LD.items() if k!=KEY), 'label used elsewhere'
assert os.path.isdir(R+'/usr/local/libexec')
if os.path.exists(R+BIN): assert sha(rd(BIN))==sha(exe) or os.path.exists(R+REC), 'a different n2gpsdl_pipe without our receipt: review'
# ---- binary
if not (os.path.exists(R+BIN) and sha(rd(BIN))==sha(exe)): put(BIN, exe, 0o755, 0, 0)
print('GNSSPIPE1_BIN', BIN, sha(exe)[:12])
# ---- job
if LD.get(KEY)!=JOB:
    LD[KEY]=JOB; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('GNSSPIPE1_JOB set', json.dumps(JOB['ProgramArguments']), 'launchd', oldh[:12], '->', rc['installed_launchd_sha256'][:12])
else: print('GNSSPIPE1_JOB already')
put(REC,(json.dumps({'bin':BIN,'sha256':sha(exe),'job':KEY,'launchd_before':oldh},indent=1)+'\n').encode(),0o644,0,0)
if not os.path.exists(R+GATE): put(GATE,b'run\n',0o644,0,0)
print('GNSSPIPE1_GATE on', GATE)
print('CC_JOB_ADDED', LABEL); print('CC_RECEIPT_UPDATED metal receipt launchd sha')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
