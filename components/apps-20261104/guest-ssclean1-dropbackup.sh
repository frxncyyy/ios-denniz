set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, plistlib, shutil, zipfile
R='/work/root'
# ssclean1 dropbackup: delete the kept modded bundle /private/var/db/nord2-ssclean1/SubwaySurf.app (~409 MiB). The receipt stays
# (backup: dropped); afterwards only payload-ssclean1-undoipa (re-extracts the modded IPA) can undo.
plan=json.load(open('/src/plan.json')); OLD=json.load(open('/src/old-manifest.json')); NEW=json.load(open('/src/new-manifest.json'))
mh=lambda m: hashlib.sha256(json.dumps(m,sort_keys=True).encode()).hexdigest()
assert mh(OLD)==plan['old']['manifest_sha256'] and mh(NEW)==plan['new']['manifest_sha256'], 'manifest mismatch'
APP=R+plan['bundle']; BAK=R+plan['backup']; DB=os.path.dirname(BAK); RCP=R+plan['receipt']; SUF=plan['csstore_suffix']
def fsha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda: f.read(1<<20), b''): h.update(b)
    return h.hexdigest()
def matches(d, m):
    """True if tree d is exactly manifest m (paths, file sha256, file modes); else (False, reason)."""
    if not os.path.isdir(d) or os.path.islink(d): return False, 'missing'
    files, dirs = {}, set()
    for b, dn, fn in os.walk(d):
        for n in dn:
            p=os.path.join(b,n); assert not os.path.islink(p), 'symlink '+p; dirs.add(os.path.relpath(p,d))
        for n in fn:
            p=os.path.join(b,n); assert not os.path.islink(p) and os.path.isfile(p), 'non-regular '+p
            files[os.path.relpath(p,d)]=p
    if dirs!=set(m['dirs']): return False, 'dirs differ: %r'%sorted(dirs^set(m['dirs']))[:5]
    if set(files)!=set(m['files']): return False, 'files differ: %r'%sorted(set(files)^set(m['files']))[:5]
    for rel, p in files.items():
        sha, mode, size = m['files'][rel]
        st=os.stat(p)
        if st.st_size!=size or (st.st_mode&0o7777)!=mode: return False, 'size/mode differ: '+rel
        if fsha(p)!=sha: return False, 'content differs: '+rel
    return True, 'ok'
def free_mib():
    st=os.statvfs(R); return st.f_bavail*st.f_frsize//2**20
def extract(ipa, sha, m, dst):
    """guest-install.sh step 2, verbatim rule."""
    assert fsha(ipa)==sha, 'IPA sha256 mismatch '+ipa
    z=zipfile.ZipFile(ipa); root='Payload/'+m['bundle']+'/'
    assert not os.path.exists(dst), dst
    for i in z.infolist():
        if not i.filename.startswith(root): continue
        rel=i.filename[len(root):]
        if not rel: continue
        assert '..' not in rel.split('/')
        p=dst+'/'+rel
        if i.filename.endswith('/'):
            os.makedirs(p,exist_ok=True); continue
        os.makedirs(os.path.dirname(p),exist_ok=True)
        data=z.read(i)
        assert (i.external_attr>>16)&0o170000!=0o120000
        with open(p,'wb') as f: f.write(data)
        macho=data[:4] in (b'\xcf\xfa\xed\xfe',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca')
        os.chmod(p,0o755 if macho else 0o644)
    for dp,dn,fn in os.walk(dst):
        for x in dn+fn: os.lchown(os.path.join(dp,x),0,0)
        os.chmod(dp,0o755); os.chown(dp,0,0)
    ip=plistlib.load(open(dst+'/Info.plist','rb'))
    assert ip['CFBundleIdentifier']==plan['bundle_id']
    assert os.stat(dst+'/'+ip['CFBundleExecutable']).st_mode&0o111
    return ip
print('SSCLEAN1_FREE_BEFORE_MB', free_mib())
ok=matches(APP,NEW); assert ok[0], 'the installed /Applications/SubwaySurf.app is not the ssclean1 bundle: '+ok[1]
assert os.path.isfile(RCP), 'no ssclean1 receipt'
if not os.path.exists(BAK):
    print('SSCLEAN1_BACKUP_ALREADY_DROPPED'); print('CC_JOB_ADDED ssclean1 dropbackup: nothing to do'); print('CC_RECEIPT_UPDATED unchanged')
else:
    ok=matches(BAK,OLD); assert ok[0], 'the kept backup is not the modded bundle: '+ok[1]
    shutil.rmtree(BAK)
    rc=json.load(open(RCP)); rc['backup']='dropped'
    tmp=RCP+'.tmp'
    with open(tmp,'w') as f: f.write(json.dumps(rc,indent=1)+'\n')
    os.chmod(tmp,0o644); os.chown(tmp,0,0); os.replace(tmp,RCP)
    print('SSCLEAN1_BACKUP_DROPPED', plan['backup'])
    print('CC_JOB_ADDED ssclean1 dropbackup'); print('CC_RECEIPT_UPDATED backup dropped')
os.sync()
print('SSCLEAN1_FREE_AFTER_MB', free_mib())
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
