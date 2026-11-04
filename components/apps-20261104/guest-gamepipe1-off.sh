set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# gamepipe1-off (payload-gamepipe1-off): the switch flag /var/db/nord2-metal/no-gamepipe made (behaviour = thumbfix5: no anatomy lines, no pipeline reuse, no async builds; next process launch);
# with payload-gamepipe1-on (a file 'remove' in the payload) the flag made here is removed again. Only over gamepipe1; nothing else is touched.
N='Nord2Metal'; NEW='4e0673355e60e7ba65dba8ca84ae1023da55fdf9cee87d070eed07a0c3dcf3d7'
FLAG=DB+'/no-gamepipe'; MARK=b'made-by-gamepipe1\n'; RP=DB+'/receipt.json'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
assert sha(B+'/'+N)==NEW, 'gamepipe1 is not installed: '+sha(B+'/'+N)
assert json.load(open(RP))['inventory'][REL][N]==NEW, 'the receipt does not name gamepipe1'
if os.path.exists('/src/remove'):
    if not os.path.exists(FLAG): print('GAMEPIPE1_ON', 'flag absent already')
    else:
        assert open(FLAG,'rb').read()==MARK, 'the no-gamepipe flag was not made by gamepipe1-off: left alone'
        os.unlink(FLAG); print('GAMEPIPE1_ON', 'no-gamepipe removed')
else:
    if os.path.exists(FLAG): print('GAMEPIPE1_OFF', 'flag present already', open(FLAG,'rb').read()[:40])
    else:
        st=os.stat(RP); tmp=FLAG+'.nord2tmp'
        with open(tmp,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
        os.chmod(tmp,0o644); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,FLAG)
        print('GAMEPIPE1_OFF', 'no-gamepipe made')
print('CC_JOB_ADDED gamepipe1 switch: no-gamepipe', 'present' if os.path.exists(FLAG) else 'absent'); print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
