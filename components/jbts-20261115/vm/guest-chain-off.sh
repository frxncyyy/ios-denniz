set +e
# jbts1 INSTALL (gpu/build/jbts-20261115, built by build_jbts.py): rootless Procursus prefix /private/var/jb (Sileo 2.5.1, OpenSSH 9.7p1,
# TrollStore Helper 2.1.1 + bundled TrollStore 2.1.1 tar and ldid) + launchd jobs com.nord2.jb.init / com.nord2.jb.sshd + SpringBoard
# SBShowNonDefaultSystemApps=true (the prefix apps are registered as system apps) + receipt /private/var/db/nord2-jbts.json.
# Idempotent: same tar already installed -> settings/jobs re-checked only (JBTS1_ALREADY). Refuses a /private/var/jb it did not make.
# Every check runs before the first write. Writes: prefix extracted to /private/var/jb.nord2tmp, renamed; ssh keys/passwords; jobs; prefs; receipt.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, plistlib, shutil, tarfile
R='/work/root'; J=R+'/private/var/jb'; TMP=J+'.nord2tmp'; REC='/private/var/db/nord2-jbts.json'
LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
SB='/private/var/mobile/Library/Preferences/com.apple.springboard.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def fsha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda: f.read(1<<20), b''): h.update(b)
    return h.hexdigest()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
plan=json.load(open('/src/plan.json')); assert plan['mode']=='install'
for n,h in plan['files'].items(): assert fsha('/src/'+n)==h, 'JBTS1_REFUSED payload file %s is not the planned one'%n
st=json.load(open('/src/ssh-settings.json')); assert st['password_sha512crypt'].startswith('$6$')
# ---- checks ----
raw=rd(LP); cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
rc=json.load(open(R+MR)); assert rc.get('installed_launchd_sha256')==sha(raw), 'JBTS1_REFUSED launchd.plist differs from the Metal receipt: not touching it'
for key,job in plan['jobs'].items():
    k='/System/Library/LaunchDaemons/'+key+'.plist'
    assert not any(isinstance(j,dict) and j.get('Label')==job['Label'] for kk,j in LD.items() if kk!=k), 'JBTS1_REFUSED label %s used elsewhere'%job['Label']
old=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if os.path.lexists(J):
    assert old is not None, 'JBTS1_REFUSED /private/var/jb exists and is not ours (no receipt): another jailbreak prefix? not touching it'
    assert os.path.isdir(J) and not os.path.islink(J), 'JBTS1_REFUSED /private/var/jb is not a directory'
    same=old.get('tar_sha256')==plan['tar_sha256']
    assert same, 'JBTS1_REFUSED a different jbts build is installed (%s): run the undo first'%old.get('tar_sha256','?')[:12]
else:
    same=False
if os.path.lexists(TMP): shutil.rmtree(TMP)
if os.path.lexists(R+SB): assert os.path.isfile(R+SB) and not os.path.islink(R+SB), 'JBTS1_REFUSED springboard prefs not a file'
sv=os.statvfs(R); free=sv.f_bavail*sv.f_frsize; need=os.path.getsize('/src/jbroot.tar')*2+(512<<20)
assert free>need, 'JBTS1_REFUSED free space %d MiB < %d MiB'%(free>>20, need>>20)
tf=tarfile.open('/src/jbroot.tar'); members=tf.getmembers()
for m in members:
    n=m.name
    assert n=='var' or n=='var/jb' or n.startswith('var/jb/'), 'JBTS1_REFUSED tar member '+n
    assert '..' not in n.split('/') and not n.startswith('/'), 'JBTS1_REFUSED tar member '+n
    assert m.isdir() or m.isfile() or m.issym(), 'JBTS1_REFUSED tar member type '+n
print('JBTS1_CHECKS_OK members', len(members), 'free_MiB', free>>20, 'already' if same else 'new')
# ---- prefix ----
if not same:
    os.mkdir(TMP); os.chmod(TMP,0o755); os.chown(TMP,0,0)
    dirs=[]
    for m in members:
        if m.name in ('var','var/jb'): continue
        p=TMP+'/'+m.name[len('var/jb/'):]
        if m.isdir():
            os.makedirs(p, exist_ok=True); dirs.append((p,m))
        elif m.issym():
            os.makedirs(os.path.dirname(p), exist_ok=True)
            if os.path.lexists(p): os.remove(p)
            os.symlink(m.linkname,p); os.lchown(p,m.uid,m.gid)
        else:
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with tf.extractfile(m) as s, open(p,'wb') as d: shutil.copyfileobj(s,d,1<<20)
            os.chown(p,m.uid,m.gid); os.chmod(p,m.mode&0o7777)   # chmod after chown: keeps set-ID bits
            os.utime(p,(m.mtime,m.mtime))
    for p,m in reversed(dirs):
        os.chown(p,m.uid,m.gid); os.chmod(p,m.mode&0o7777)
    os.sync()
    # verify every regular file against the tar before going live
    bad=[m.name for m in members if m.isfile() and os.path.getsize(TMP+'/'+m.name[7:])!=m.size]
    assert not bad, 'JBTS1_FAILED size mismatch '+repr(bad[:5])
    os.rename(TMP,J); os.chown(J,0,0); os.chmod(J,0o755)
    print('JBTS1_PREFIX extracted', len(members), 'entries ->', J[len(R):])
# ---- ssh: host keys, password hashes for root/mobile, authorized_keys ----
for kt in ('ed25519','ecdsa','rsa'):
    for suf,mode in (('',0o600),('.pub',0o644)):
        n='ssh_host_%s_key%s'%(kt,suf); b=open('/src/'+n,'rb').read(); p='/private/var/jb/etc/ssh/'+n
        if not (os.path.exists(R+p) and rd(p)==b and (os.stat(R+p).st_mode&0o777)==mode): put(p,b,mode,0,0)
mpp='/private/var/jb/etc/master.passwd'; lines=rd(mpp).decode().split('\n'); out=[]
for l in lines:
    f=l.split(':')
    if len(f)>=10 and f[0] in ('root','mobile'): f[1]=st['password_sha512crypt']; l=':'.join(f)
    out.append(l)
mp='\n'.join(out).encode()
if mp!=rd(mpp): put(mpp,mp,0o600,0,0)
assert b'\nsshd:' in mp and b'\nroot:$6$' in mp and b'\nmobile:$6$' in mp
for home,uid in (('/private/var/jb/var/root',0),('/private/var/jb/var/mobile',501)):
    if st['authorized_keys']:
        if not os.path.isdir(R+home+'/.ssh'): os.mkdir(R+home+'/.ssh')
        os.chown(R+home+'/.ssh',uid,uid); os.chmod(R+home+'/.ssh',0o700)
        put(home+'/.ssh/authorized_keys',st['authorized_keys'].encode(),0o600,uid,uid)
for fl in plan.get('flags_set',[]):
    p='/private/var/jb/etc/nord2-jb/'+fl
    if not same and not os.path.exists(R+p): put(p,b'1\n',0o644,0,0)
print('JBTS1_SSH keys+passwords set, authorized_keys', bool(st['authorized_keys']), 'flags', plan.get('flags_set'))
# ---- SpringBoard: show non-default system apps (Sileo/TrollStore Helper registered by uicache as system apps) ----
prev=None
if os.path.exists(R+SB):
    sraw=rd(SB); fmt=plistlib.FMT_BINARY if sraw[:6]==b'bplist' else plistlib.FMT_XML; sp=plistlib.loads(sraw); s0=os.stat(R+SB)
    prev=sp.get('SBShowNonDefaultSystemApps','<absent>'); mode,uid,gid=s0.st_mode&0o7777,s0.st_uid,s0.st_gid
else:
    sp={}; fmt=plistlib.FMT_BINARY; prev='<nofile>'; mode,uid,gid=0o600,501,501
if sp.get('SBShowNonDefaultSystemApps') is not True:
    sp['SBShowNonDefaultSystemApps']=True; put(SB,plistlib.dumps(sp,fmt=fmt),mode,uid,gid)
print('JBTS1_SPRINGBOARD SBShowNonDefaultSystemApps true (was %r)'%(prev,))
# ---- launchd jobs (Metal receipt keeps the launchd.plist sha) ----
raw=rd(LP); cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']; changed=False
for key,job in plan['jobs'].items():
    k='/System/Library/LaunchDaemons/'+key+'.plist'
    if LD.get(k)!=job: LD[k]=job; changed=True
if changed:
    fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
print('JBTS1_JOBS', 'set' if changed else 'already', sorted(plan['jobs']), 'launchd ->', sha(rd(LP))[:12])
# ---- receipt ----
rec={'payload':'jbts1','tar_sha256':plan['tar_sha256'],'prefix':'/private/var/jb','jobs':sorted(plan['jobs']),
     'springboard_prev_SBShowNonDefaultSystemApps': (old or {}).get('springboard_prev_SBShowNonDefaultSystemApps', prev),
     'installed_at_build':plan['built'],'packages':plan['packages']}
b=(json.dumps(rec,indent=1)+'\n').encode()
if not (os.path.exists(R+REC) and rd(REC)==b): put(REC,b,0o644,0,0)
print('JBTS1_ALREADY' if same else 'JBTS1_INSTALLED', plan['tar_sha256'][:12])
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'JBTS1_OK'
echo 'CC_JOB_ADDED jbts1 (com.nord2.jb.init + com.nord2.jb.sshd)'
echo 'CC_RECEIPT_UPDATED metal receipt launchd sha + /private/var/db/nord2-jbts.json'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
set +e
# jbts1 kill switches from the PC (payload-jbts1-off|-on|-sshoff|-sshon): files in /private/var/jb/etc/nord2-jb read at every boot by
# jbinit.sh / sshd-run.sh: 'disabled' (whole jbts1 boot side off) and 'ssh-disabled' (SSH server off). Nothing else is touched.
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import json, os
R='/work/root'; D=R+'/private/var/jb/etc/nord2-jb'
plan=json.load(open('/src/plan2.json')); assert plan['mode']=='flags'
assert os.path.exists(R+'/private/var/db/nord2-jbts.json') and os.path.isdir(D), 'JBTS1_FLAGS_REFUSED jbts1 is not installed'
for f in plan['flags_set']:
    if not os.path.exists(D+'/'+f):
        with open(D+'/'+f,'w') as o: o.write('1\n')
        os.chown(D+'/'+f,0,0); os.chmod(D+'/'+f,0o644)
for f in plan['flags_clear']:
    if os.path.lexists(D+'/'+f): os.remove(D+'/'+f)
print('JBTS1_FLAGS now', sorted(os.listdir(D)))
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'JBTS1_FLAGS_OK'
echo 'CC_JOB_ADDED jbts1-flags'
echo 'CC_RECEIPT_UPDATED none'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
