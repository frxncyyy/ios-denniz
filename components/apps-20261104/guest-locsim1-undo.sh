set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# undo of guest-locsim1.sh: the six Simulated* keys back to the receipt values (absent -> removed); receipt dir removed
import os, plistlib, json, shutil
R='/work/root'
P=R+'/private/var/mobile/Library/Preferences/com.apple.locationd.plist'
K=R+'/private/var/db/nord2-locsim'; M=K+'/receipt.json'
def put(path, data, mode, uid, gid):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
if not os.path.isfile(M):
    assert not os.path.exists(K), '%s without receipt: review'%K
    print('LOCSIM1_UNDO_ALREADY (no receipt)')
else:
    rec=json.load(open(M)); b=open(P,'rb').read(); d=plistlib.loads(b)
    fmt=plistlib.FMT_BINARY if b[:8]==b'bplist00' else plistlib.FMT_XML
    for k,v in rec.items():
        if v=='<absent>': d.pop(k, None)
        else: d[k]=v
    st=os.stat(P); put(P, plistlib.dumps(d, fmt=fmt), st.st_mode&0o7777, st.st_uid, st.st_gid)
    shutil.rmtree(K); print('LOCSIM1_UNDONE', {k: d.get(k, '<absent>') for k in rec})
print('CC_JOB_ADDED undo locsim1')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
