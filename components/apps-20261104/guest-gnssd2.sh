set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# gnssd2: GNSS engine daemon, gnssd1 + TPIDR_EL0 canary fix + import trace + fault handler (installs over gnssd1 in place;
# same paths, same receipt, same gnssstep gate name .off-gnssd1, so guest-gnssd1-undo.sh also undoes it). /usr/local/libexec/n2mnld (gpu/hwcodec/gnss/n2mnld/native, libmnl embedded, ad-hoc signed)
# + config dir /private/var/db/n2gnss (0700: MNL_Config.xml empty = harness config, MNL_Config.vendor.xml kept for later)
# + launchd job com.nord2.gnssd (RunAtLoad, one run per boot, gate /private/var/db/nord2-gnssd).
# The gnssstep probe job (n2gpsdl_step, ladders K=..) also opens /dev/gpsdl0 at boot: its gate /private/var/db/nord2-gnssstep
# is moved to /private/var/db/nord2-gnssstep.off-gnssd1 (the job itself stays; it exits at once without its gate). Undo restores.
# launchd.plist edited like guest-vcodec.sh (Metal receipt installed_launchd_sha256 checked before, updated after).
import os, json, hashlib, plistlib
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2mnld'; DIR='/private/var/db/n2gnss'; GATE='/private/var/db/nord2-gnssd'; REC='/private/var/db/nord2-gnssd.json'
SGATE='/private/var/db/nord2-gnssstep'; SOFF=SGATE+'.off-gnssd1'
LABEL='com.nord2.gnssd'; KEY='/System/Library/LaunchDaemons/'+LABEL+'.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
plan=json.load(open('/src/plan.json'))
F={n:open('/src/'+n,'rb').read() for n in ('n2mnld','MNL_Config.xml','MNL_Config.vendor.xml')}
for n,b in F.items(): assert sha(b)==plan['files'][n], '%s is not the planned file'%n
exe=F['n2mnld']; assert exe[:4]==b'\xcf\xfa\xed\xfe' and b'__MNLT' in exe, 'not the n2mnld Mach-O'
JOB={'Label':LABEL,'UserName':'root','RunAtLoad':True,'ProcessType':'Interactive','EnablePressuredExit':False,
     'ProgramArguments':[BIN,'--gate',GATE]+[str(a) for a in plan['args']]}
# ---- checks first
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
assert not any(isinstance(j,dict) and j.get('Label')==LABEL for k,j in LD.items() if k!=KEY), 'label used elsewhere'
assert os.path.isdir(R+'/usr/local/libexec')
if os.path.exists(R+BIN): assert sha(rd(BIN))==sha(exe) or os.path.exists(R+REC), 'a different n2mnld without our receipt: review'
if os.path.exists(R+DIR): assert os.path.isdir(R+DIR) and (os.path.exists(R+REC) or not os.listdir(R+DIR)), '/private/var/db/n2gnss exists without our receipt: review'
old_rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
# ---- binary + config dir
if not (os.path.exists(R+BIN) and sha(rd(BIN))==sha(exe)): put(BIN, exe, 0o755, 0, 0)
dir_made=old_rec.get('dir_made', not os.path.exists(R+DIR))
if not os.path.isdir(R+DIR): os.makedirs(R+DIR)
os.chmod(R+DIR,0o700); os.chown(R+DIR,0,0)
for n in ('MNL_Config.xml','MNL_Config.vendor.xml'):
    p=DIR+'/'+n
    if not (os.path.exists(R+p) and rd(p)==F[n]): put(p, F[n], 0o600, 0, 0)
print('GNSSD2_FILES', BIN, sha(exe)[:12], DIR, 'MNL_Config.xml', len(F['MNL_Config.xml']), 'bytes')
# ---- gnssstep gate off (nothing else may open /dev/gpsdl0 at boot)
moved=old_rec.get('gnssstep_gate_moved', False)
if os.path.exists(R+SGATE):
    if os.path.exists(R+SOFF): os.remove(R+SOFF)
    os.rename(R+SGATE, R+SOFF); moved=True; print('GNSSD2_GNSSSTEP gate moved', SGATE, '->', SOFF)
else: print('GNSSD2_GNSSSTEP gate', 'already off' if os.path.exists(R+SOFF) else 'absent')
# ---- job
if LD.get(KEY)!=JOB:
    LD[KEY]=JOB; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('GNSSD2_JOB set', json.dumps(JOB['ProgramArguments']), 'launchd', oldh[:12], '->', rc['installed_launchd_sha256'][:12])
else: print('GNSSD2_JOB already')
put(REC,(json.dumps({'bin':BIN,'sha256':sha(exe),'dir':DIR,'dir_made':dir_made,'job':KEY,'gnssstep_gate_moved':moved,'payload':'gnssd2',
                     'launchd_before':old_rec.get('launchd_before',oldh)},indent=1)+'\n').encode(),0o644,0,0)
if not os.path.exists(R+GATE): put(GATE,b'run\n',0o644,0,0)
print('GNSSD2_GATE on', GATE)
print('CC_JOB_ADDED', LABEL); print('CC_RECEIPT_UPDATED metal receipt launchd sha')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
