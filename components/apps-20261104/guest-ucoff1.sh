set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# ucoff1 (payload-gpufinal3-usecountoff): ONLY the switch /var/db/nord2-metal/usecount-off (content 'made-by-ucoff1'), no driver change.
# With it every process's Nord2Metal (gpufinal3 y2s93 / gpufinal4 y2s94) takes no y2s84 texture use-count holds from its NEXT launch
# (backboardd: respring/reboot) -- the camera preview-freeze experiment. Refused unless the installed Nord2Metal is gpufinal3 or gpufinal4
# (the flag means something else on older drivers) and the bundle matches its receipt. A usecount-off already there and not ours is kept.
# Receipt not changed. Undo: guest-ucoff1-undo.sh removes the file only if it carries the marker.
N='Nord2Metal'; S='/src/'; FL='usecount-off'; MARK=b'made-by-ucoff1\n'
OK={'e31a9b073095a22d0bd3fe07e6dc72f6075fa669b2377ea76aded1eb97a0f3f0': 'gpufinal3 (y2s93-final)', '015f781d7f9dfa91ef012af8c903c11de0c8a313fd5b9ea895568bd895c5053f': 'gpufinal4 (y2s94-final)'}
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
rc=json.load(open(DB+'/receipt.json'))
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
cur=sha(B+'/'+N); assert rc['inventory'][REL][N]==cur, 'receipt/bundle mismatch for Nord2Metal'
assert cur in OK, 'the installed Nord2Metal is neither gpufinal3 nor gpufinal4: %s'%cur
assert open(S+FL,'rb').read()==MARK, 'payload flag file is not the ucoff1 one'
dst=DB+'/'+FL
assert not os.path.exists(dst+'.nord2tmp'), 'leftover .nord2tmp'
if not os.path.exists(dst):
    with open(dst,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
    os.chmod(dst,0o644); print('UCOFF1_SET usecount-off created (driver %s)'%OK[cur])
elif open(dst,'rb').read()==MARK: print('UCOFF1_ALREADY usecount-off (ours) present')
else: print('UCOFF1_KEPT usecount-off present and not ours (%r): left as is, holds are off anyway'%open(dst,'rb').read(64))
assert os.path.isfile(dst)
print('CC_JOB_ADDED ucoff1: switch /var/db/nord2-metal/usecount-off (no texture holds at next process launch)')
print('CC_RECEIPT_UPDATED unchanged (switch file only)')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
