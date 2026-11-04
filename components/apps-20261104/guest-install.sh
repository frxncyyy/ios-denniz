set +e
fsck.hfsplus -f -n /dev/vda | tail -3
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, json, shutil, stat, struct, zipfile, hashlib, glob, plistlib
R='/work/root'
plan=json.load(open('/src/plan.json'))
def used():
    st=os.statvfs(R); return st.f_bavail*st.f_frsize
print('FREE_BEFORE_MB', used()//2**20)

# 1. free space: keep only the latest Metal update backup (receipt.last_update); older ones are rollback copies nobody uses
state=R+'/private/var/db/nord2-metal'
rc=json.load(open(state+'/receipt.json'))
keep=os.path.basename(rc['last_update'])
assert os.path.isdir(state+'/updates/'+keep)
n=0
for d in sorted(os.listdir(state+'/updates')):
    if d==keep: continue
    shutil.rmtree(state+'/updates/'+d); n+=1
print('REMOVED_UPDATE_BACKUPS', n, 'kept', keep[:12])
# unified-log data files (logd recreates them); keep directory layout, timesync and version
freed=0
for sub in ('Persist','Special','Signpost','HighVolume'):
    for f in glob.glob(R+'/private/var/db/diagnostics/%s/*.tracev3'%sub):
        freed+=os.lstat(f).st_size; os.unlink(f)
for f in glob.glob(R+'/private/var/db/diagnostics/logdata.statistics.*'):
    freed+=os.lstat(f).st_size; os.unlink(f)
print('REMOVED_LOG_MB', freed//2**20)
os.sync()
print('FREE_AFTER_CLEANUP_MB', used()//2**20)

# 2. install the apps like system apps
for app in plan['apps']:
    z=zipfile.ZipFile('/src/apps/'+app['ipa'])
    assert hashlib.sha256(open('/src/apps/'+app['ipa'],'rb').read()).hexdigest()==app['sha256']
    root='Payload/'+app['bundle']+'/'
    dst=R+'/Applications/'+app['bundle']
    assert not os.path.exists(dst), dst
    names=[i for i in z.infolist() if i.filename.startswith(root)]
    for i in names:
        rel=i.filename[len(root):]
        if not rel: continue
        assert '..' not in rel.split('/')
        p=dst+'/'+rel
        if i.filename.endswith('/'):
            os.makedirs(p,exist_ok=True); continue
        os.makedirs(os.path.dirname(p),exist_ok=True)
        data=z.read(i)
        mode=(i.external_attr>>16)&0o170000
        if mode==0o120000:
            os.symlink(data.decode(),p); continue
        with open(p,'wb') as f: f.write(data)
        macho=data[:4] in (b'\xcf\xfa\xed\xfe',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca')
        os.chmod(p,0o755 if macho else 0o644)
    for dp,dn,fn in os.walk(dst):
        for x in dn+fn:
            q=os.path.join(dp,x)
            if not os.path.islink(q): os.chown(q,0,0)
            else: os.lchown(q,0,0)
        if not os.path.islink(dp): os.chmod(dp,0o755); os.chown(dp,0,0)
    os.chown(dst,0,0); os.chmod(dst,0o755)
    ip=plistlib.load(open(dst+'/Info.plist','rb'))
    assert ip['CFBundleIdentifier']==app['bundle_id']
    exe=dst+'/'+ip['CFBundleExecutable']
    assert os.stat(exe).st_mode&0o111
    print('INSTALLED', app['bundle'], app['bundle_id'], sum(os.lstat(os.path.join(dp,f)).st_size for dp,_,fs in os.walk(dst) for f in fs)//2**20,'MB')

# 3. make LaunchServices rescan /Applications at next boot: move the caches aside (not deleted)
moved=[]
for f in glob.glob(R+'/private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/*LaunchServices*.csstore'):
    os.rename(f, f+'.nord2-prev'); moved.append(os.path.basename(f))
print('LS_CACHES_MOVED', moved)
os.sync()
print('FREE_AFTER_INSTALL_MB', used()//2**20)
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
