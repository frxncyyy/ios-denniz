set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# photosperf1-off (payload-photosperf1-off): the switch flag /var/db/nord2-metal/no-photosperf made (behaviour = thumbfix5, counters stay, opt=0);
# with payload-photosperf1-on (a file 'remove' in the payload) the flag made here is removed again. Only over photosperf1; nothing else is touched.
N='Nord2Metal'; NEW='b5e8a282738e6c457e3444668f2f3db7ca05df6852f86c91e072e1937b1f4b3f'
FLAG=DB+'/no-photosperf'; MARK=b'made-by-photosperf1\n'; RP=DB+'/receipt.json'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
assert sha(B+'/'+N)==NEW, 'photosperf1 is not installed: '+sha(B+'/'+N)
assert json.load(open(RP))['inventory'][REL][N]==NEW, 'the receipt does not name photosperf1'
if os.path.exists('/src/remove'):
    if not os.path.exists(FLAG): print('PHOTOSPERF1_ON', 'flag absent already')
    else:
        assert open(FLAG,'rb').read()==MARK, 'the no-photosperf flag was not made by photosperf1-off: left alone'
        os.unlink(FLAG); print('PHOTOSPERF1_ON', 'no-photosperf removed')
else:
    if os.path.exists(FLAG): print('PHOTOSPERF1_OFF', 'flag present already', open(FLAG,'rb').read()[:40])
    else:
        st=os.stat(RP); tmp=FLAG+'.nord2tmp'
        with open(tmp,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
        os.chmod(tmp,0o644); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,FLAG)
        print('PHOTOSPERF1_OFF', 'no-photosperf made')
print('CC_JOB_ADDED photosperf1 switch: no-photosperf', 'present' if os.path.exists(FLAG) else 'absent'); print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
