set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# locbridge1: NMEA -> locationd bridge. /usr/local/libexec/n2locbridge (gpu/hwcodec/locbridge, ad-hoc signed with the
# entitlement com.apple.locationd.simulation) + recorded log /usr/local/share/n2gnss/sample-drive.nmea + launchd job
# com.nord2.locbridge (RunAtLoad, KeepAlive {SuccessfulExit: false}, gate /private/var/db/nord2-locbridge). Arguments from
# plan.json: replay of the log in a loop (first test), or --fifo /private/var/run/n2gnss.nmea (any NMEA producer).
# Console "N2LOC ...", log /private/var/db/nord2-locbridge.log. launchd.plist edited like guest-vcodec.sh (Metal receipt).
import os, json, hashlib, plistlib
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2locbridge'; SH='/usr/local/share/n2gnss'; LOG=SH+'/sample-drive.nmea'
GATE='/private/var/db/nord2-locbridge'; REC='/private/var/db/nord2-locbridge.json'
LABEL='com.nord2.locbridge'; KEY='/System/Library/LaunchDaemons/'+LABEL+'.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
plan=json.load(open('/src/plan.json')); exe=open('/src/n2locbridge','rb').read(); nmea=open('/src/sample-drive.nmea','rb').read()
assert sha(exe)==plan['files']['n2locbridge'] and sha(nmea)==plan['files']['sample-drive.nmea'], 'payload files are not the planned ones'
assert exe[:4]==b'\xcf\xfa\xed\xfe' and b'com.apple.locationd.simulation' in exe, 'binary without its entitlement'
JOB={'Label':LABEL,'UserName':'root','RunAtLoad':True,'KeepAlive':{'SuccessfulExit':False},'ThrottleInterval':30,
     'ProcessType':'Interactive','EnablePressuredExit':False,
     'ProgramArguments':[BIN,'--gate',GATE,'--log','/private/var/db/nord2-locbridge.log']+[str(a) for a in plan['args']]}
# ---- checks first
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
assert not any(isinstance(j,dict) and j.get('Label')==LABEL for k,j in LD.items() if k!=KEY), 'label used elsewhere'
assert os.path.isdir(R+'/usr/local/libexec')
for p,b in ((BIN,exe),(LOG,nmea)):
    if os.path.exists(R+p): assert sha(rd(p))==sha(b) or os.path.exists(R+REC), 'a different %s without our receipt: review'%p
# ---- files
if not os.path.isdir(R+SH): os.makedirs(R+SH); os.chmod(R+SH,0o755); os.chown(R+SH,0,0)
for p,b,m in ((BIN,exe,0o755),(LOG,nmea,0o644)):
    if not (os.path.exists(R+p) and sha(rd(p))==sha(b)): put(p,b,m,0,0)
print('LOCBRIDGE1_FILES', BIN, sha(exe)[:12], LOG, sha(nmea)[:12])
# ---- job
if LD.get(KEY)!=JOB:
    LD[KEY]=JOB; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('LOCBRIDGE1_JOB set', json.dumps(JOB['ProgramArguments']), 'launchd', oldh[:12], '->', rc['installed_launchd_sha256'][:12])
else: print('LOCBRIDGE1_JOB already')
put(REC,(json.dumps({'bin':BIN,'sha256':sha(exe),'log':LOG,'job':KEY},indent=1)+'\n').encode(),0o644,0,0)
if not os.path.exists(R+GATE): put(GATE,b'run\n',0o644,0,0)
print('LOCBRIDGE1_GATE on', GATE)
print('CC_JOB_ADDED', LABEL); print('CC_RECEIPT_UPDATED metal receipt launchd sha')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
