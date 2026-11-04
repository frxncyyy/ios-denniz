set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-ispfeat-fold.sh (copies: -fold, -r32, -mrt, -all; only WANT differs): writes the libmerge5 ISP feature switch
# /private/var/db/nord2-isp-features (root:wheel 644) with the text /src/nord2-isp-features (payload-ispfeat-<word>: the word + newline).
# libmerge5 (lm5) reads it once at the first rwa_isp_create: words fold / r32 / mrt / all turn on libmerge4's three GPU changes.
# The marker /private/var/db/nord2-metal/isp-features.made-by-ispfeat records that we made it and the text we wrote; guest-ispfeat-undo.sh
# removes only that. If our own switch holds another variant it is rewritten (one txn moves fold -> r32 etc.). A switch not made by us is
# accepted only if it already holds the same text, otherwise refused. Nothing else is touched.
import os
WANT='fold'
R='/work/root'; G='/private/var/db/nord2-isp-features'; MK='/private/var/db/nord2-metal/isp-features.made-by-ispfeat'
HDR=b'ispfeat created /private/var/db/nord2-isp-features with:\n'
assert os.path.isdir(R+'/private/var/db/nord2-metal'), 'no /private/var/db/nord2-metal'
text=open('/src/nord2-isp-features','rb').read()
assert text==(WANT+'\n').encode(), 'payload is not payload-ispfeat-%s: %r'%(WANT, text[:64])
def put(path, data):
    tmp=R+path+'.nord2tmp'
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,0o644); os.chown(tmp,0,0); os.replace(tmp,R+path)
    assert open(R+path,'rb').read()==data
def regular(p): return os.path.isfile(R+p) and not os.path.islink(R+p)
if os.path.lexists(R+G):
    assert regular(G), 'not a regular file: '+G
    have=open(R+G,'rb').read()
    if os.path.lexists(R+MK):
        assert regular(MK), 'not a regular file: '+MK
        mine=open(R+MK,'rb').read()
        assert mine.startswith(HDR) and mine[len(HDR):]==have, 'the switch is no longer what ispfeat wrote (%r, marker %r): review'%(have[:64], mine[len(HDR):][:64])
        if have==text:
            print('ISPFEAT_SWITCH already there', G, repr(have), 'made by ispfeat'); print('CC_JOB_ADDED ispfeat-%s already there'%WANT); print('CC_RECEIPT_UPDATED unchanged')
        else:
            put(MK, HDR+text); put(G, text)
            print('ISPFEAT_SWITCH changed', G, repr(have), '->', repr(text), '(ours, marker updated)')
            print('CC_JOB_ADDED ispfeat-%s: ISP features %r (was %r)'%(WANT, text, have)); print('CC_RECEIPT_UPDATED unchanged (no addon receipt change)')
    else:
        assert have==text, 'a switch NOT made by ispfeat asks something else: %r (want %r): review'%(have[:64], text)
        print('ISPFEAT_SWITCH already there', G, repr(have), 'NOT made by ispfeat (left as is)'); print('CC_JOB_ADDED ispfeat-%s already there'%WANT); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert not os.path.lexists(R+MK), 'ispfeat marker without the switch: review'
    put(MK, HDR+text); put(G, text)
    print('ISPFEAT_SWITCH on', G, repr(text), 'marker', MK)
    print('CC_JOB_ADDED ispfeat-%s: ISP features %r'%(WANT, text)); print('CC_RECEIPT_UPDATED unchanged (no addon receipt change)')
st=os.stat(R+G); print('ISPFEAT_STATE', repr(open(R+G,'rb').read()), 'mode %o %d:%d %d bytes'%(st.st_mode&0o7777, st.st_uid, st.st_gid, st.st_size),
                       sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-isp')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
