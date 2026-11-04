set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, plistlib, shutil, zipfile
R='/work/root'
# ssclean1 undo (payload-ssclean1-undo or payload-ssclean1-undoipa): back to the modded 3.71.0 bundle of the first install.
# Kept backup present: delete the clean bundle, rename the backup back (same CNIDs/extents = byte for byte).
# Backup dropped (-dropbackup ran): re-extract /src/apps/<modded IPA> like guest-install.sh and verify every file against the manifest.
# LaunchServices: the rebuilt csstores are deleted and the *.pre-ssclean1 ones renamed back (LS rescans if they are older than the bundle).
# Then /private/var/db/nord2-ssclean1 is removed.
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
if matches(APP,OLD)[0] and not os.path.exists(DB):
    print('SSCLEAN1_UNDO_ALREADY'); print('CC_JOB_ADDED ssclean1 undo: already the modded bundle'); print('CC_RECEIPT_UPDATED unchanged')
else:
    ok=matches(APP,NEW); assert ok[0], 'the installed /Applications/SubwaySurf.app is not the ssclean1 bundle: '+ok[1]
    assert os.path.isfile(RCP), 'no ssclean1 receipt: review'
    if os.path.exists(BAK):
        ok=matches(BAK,OLD); assert ok[0], 'the kept backup is not the modded bundle: '+ok[1]
        shutil.rmtree(APP); os.rename(BAK,APP); how='renamed back from '+plan['backup']
    else:
        ipa='/src/apps/'+plan['old']['ipa']
        assert os.path.isfile(ipa), 'backup dropped: use payload-ssclean1-undoipa (needs the modded IPA)'
        shutil.rmtree(APP); os.sync()
        need=plan['old']['allocated_mib']+plan['margin_mib']; f0=free_mib()
        assert f0>=need, 'not enough rootfs space: %d MiB free, need %d'%(f0,need)
        extract(ipa, plan['old']['sha256'], OLD, APP); os.chown(APP,0,0); os.chmod(APP,0o755); how='re-extracted from '+plan['old']['ipa']
    ok=matches(APP,OLD); assert ok[0], 'restored bundle verify failed: '+ok[1]
    print('SSCLEAN1_UNDONE SubwaySurf.app = modded 3.71.0,', how)
    put=[]
    for c in plan['csstores']:
        if os.path.exists(R+c+SUF):
            if os.path.exists(R+c): os.unlink(R+c)
            os.rename(R+c+SUF, R+c); put.append(os.path.basename(c))
        elif os.path.exists(R+c): os.unlink(R+c); put.append(os.path.basename(c)+' (deleted, LS rebuilds)')
    print('LS_CACHES_RESTORED', put)
    os.unlink(RCP); os.rmdir(DB)
    print('CC_JOB_ADDED ssclean1 undo:', how)
    print('CC_RECEIPT_UPDATED removed', plan['receipt'])
os.sync()
print('SSCLEAN1_FREE_AFTER_MB', free_mib())
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
