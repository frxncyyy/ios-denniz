set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
R=/work/root
echo "=== install.json"; cat $R/private/var/db/nord2-vcodec/install.json 2>&1 | head -c 6000; echo
echo "=== db dir"; ls -la $R/private/var/db/nord2-vcodec/ 2>&1 | head -40
echo "=== switches"; ls -la $R/private/var/db/ | grep -i 'nord2-video\|nord2-vcodec\|nord2-camera'
echo "=== plugins"; ls -la $R/System/Library/VideoEncoders $R/System/Library/VideoDecoders 2>&1; sha256sum $R/System/Library/VideoEncoders/Slim.videoencoder $R/System/Library/VideoDecoders/Slim.videodecoder $R/usr/local/libexec/vcodecd $R/usr/local/libexec/* 2>&1 | head -20
echo "=== vendor"; ls -la $R/usr/local/lib/n2vendor/* 2>&1 | head -30
echo "=== launchd"; python3 - <<'PY'
import plistlib
d=plistlib.loads(open('/work/root/System/Library/xpc/launchd.plist','rb').read())['LaunchDaemons']
for k,v in d.items():
    if 'vcodec' in k.lower() or 'jpeg' in k.lower(): print(k, v)
PY
echo "=== vcodecd.log tail"; ls -la $R/private/var/db/nord2-vcodec/*.log* 2>&1; tail -c 12000 $R/private/var/db/nord2-vcodec/vcodecd.log 2>&1
echo "=== camera pref"; python3 - <<'PY'
import plistlib,glob
for p in glob.glob('/work/root/private/var/mobile/Library/Preferences/com.apple.camera.plist'):
    try: d=plistlib.load(open(p,'rb')); print({k:v for k,v in d.items() if 'Encoding' in k or 'Video' in k or 'HEVC' in k or 'Format' in k})
    except Exception as e: print(e)
PY
echo "=== DCIM"; ls -la $R/private/var/mobile/Media/DCIM/*/ 2>&1 | tail -30
umount $R
echo INSPECT_DONE
