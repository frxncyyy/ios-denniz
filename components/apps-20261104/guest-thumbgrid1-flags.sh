set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# thumbgrid1 switches: the payload's file 'flags' lists '+NAME' (make /var/db/nord2-metal/NAME, content 'made-by-thumbgrid1') or
# '-NAME' (remove it, only if made here) or '-*' (remove every flag made here). Only over thumbgrid1; only the names below.
# Read once per process by the driver/library: they take effect at the next launch of MobileSlideShow (or a respring for all).
NEW='5752a629cb3df4109138a93ad77aef4b132caf8cfcb44d455454c3cfffb2ccbf'; MARK=b'made-by-thumbgrid1\n'; RP=DB+'/receipt.json'
ALLOWED=['no-thumbgrid']+['no-thumbgrid-'+n for n in ('exact','live','pending','rttouch','check','bufpool','texpool','thumbcache','async','ubo','log')]+['thumbgrid-pools-all','thumbgrid-async-all','thumbgrid-check-all','thumbgrid-rttouch-all']
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
assert sha(B+'/Nord2Metal')==NEW, 'thumbgrid1 is not installed: '+sha(B+'/Nord2Metal')
assert json.load(open(RP))['inventory'][REL]['Nord2Metal']==NEW, 'the receipt does not name thumbgrid1'
ops=[l.strip() for l in open('/src/flags').read().split('\n') if l.strip() and not l.startswith('#')]
for op in ops:
    assert op[0] in '+-' and (op=='-*' or op[1:] in ALLOWED), 'not a thumbgrid1 switch: '+op
st=os.stat(RP)
for op in ops:
    names=[n for n in ALLOWED if os.path.isfile(DB+'/'+n) and open(DB+'/'+n,'rb').read()==MARK] if op=='-*' else [op[1:]]
    for n in names:
        p=DB+'/'+n
        if op[0]=='+':
            if os.path.exists(p): print('THUMBGRID1_FLAG', n, 'present already'); continue
            tmp=p+'.nord2tmp'
            with open(tmp,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
            os.chmod(tmp,0o644); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,p); print('THUMBGRID1_FLAG', n, 'made')
        else:
            if not os.path.exists(p): print('THUMBGRID1_FLAG', n, 'absent already'); continue
            assert open(p,'rb').read()==MARK, n+' was not made by thumbgrid1: left alone'
            os.unlink(p); print('THUMBGRID1_FLAG', n, 'removed')
print('CC_JOB_ADDED thumbgrid1 switches:', sorted(n for n in ALLOWED if os.path.exists(DB+'/'+n)) or 'none'); print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
