set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os, plistlib
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
NEW='8e90184ffdf634c03dbf3f317f28aa4b35b98546210457d2a257cb1a3c25edfa'      # camera-userland-20261109/system/D22-still4096/AVCaptureSession.plist: the back cameras' photo format 4096x3072
OLD='b66f008bdf095aa818db90651bdee3f3103f076bf3d4aa15e42f8b449ac2140e'      # the dual-wide file (guest-camdual.sh): the back cameras' photo format 4032x3024
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def put(path, data, mode, uid, gid):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
def photo_sizes(av):
    # (stream format, device format) of every back camera that has formats of its own: both must say the same
    out=[]
    for d in av['AVCaptureDevices']:
        ms=d.get('MultiStream', {})
        if d.get('position')==1 and 'FigCaptureStreamFormats' in ms:
            p=ms['FigCaptureStreamFormats']['Photo']; m=ms['AVCaptureDeviceFormats']['12MP']
            assert m.get('CaptureStreamFormat')=='Photo', '12MP is not of the Photo stream format'
            out.append(((p['SensorWidth'], p['SensorHeight']), (m['Width'], m['Height'])))
    return out
# ---- check everything first: nothing is written unless all of it can be
cur=D+'/AVCaptureSession.plist'; bak=cur+'.pre-still4096'
new=open('/src/D22/AVCaptureSession.plist','rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the plist this script was written for'
want=photo_sizes(plistlib.loads(new))
assert want==[((4096,3072),(4096,3072))]*2, 'payload: the wide and the ultra-wide Photo/12MP formats are not both 4096x3072: %r'%want
ch=sha(cur)
assert ch in (NEW, OLD), 'D22/AVCaptureSession.plist is neither the dual-wide file nor the 4096x3072 one: %s (run guest-camdual.sh first)'%ch
if ch==NEW:
    assert os.path.exists(bak) and sha(bak)==OLD, '4096x3072 plist there but no dual-wide copy (*.pre-still4096): review'
    print('STILL4096_PLIST_ALREADY', NEW[:12])
else:
    was=photo_sizes(plistlib.loads(open(cur,'rb').read()))
    assert was==[((4032,3024),(4032,3024))]*2, 'the installed file is not the one expected inside: %r'%was
    st=os.stat(cur)
    put(bak, open(cur,'rb').read(), st.st_mode&0o7777, st.st_uid, st.st_gid); assert sha(bak)==OLD
    put(cur, new, st.st_mode&0o7777, st.st_uid, st.st_gid); assert sha(cur)==NEW
    print('STILL4096_PLIST_INSTALLED', NEW[:12], 'back photo formats (stream, device)', want, '(the file as it was kept as AVCaptureSession.plist.pre-still4096)')
print('CC_JOB_ADDED camera: back cameras photograph at 4096x3072 (D22/AVCaptureSession.plist)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
