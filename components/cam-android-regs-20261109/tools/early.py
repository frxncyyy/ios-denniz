# early.py <script>: compile ops, start insmod in background, (re)open camera, collect output
import sys, os, subprocess, time, shlex
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import n2d
lines = open(sys.argv[1]).read().splitlines()
ko = bytearray(open('n2d.ko', 'rb').read()); i = ko.index(b'N2DOPS01'); b = n2d.build(lines); ko[i:i + len(b)] = b
open('n2d_run.ko', 'wb').write(ko); A = n2d.ADB
subprocess.run([A, 'push', 'n2d_run.ko', '/data/local/tmp/n2de.ko'], capture_output=True)
subprocess.run([A, 'shell', 'am force-stop com.oplus.camera; input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard'], capture_output=True)
time.sleep(3); mark = 'n2dearly%d' % time.time()
p = subprocess.Popen([A, 'shell', 'su -c ' + shlex.quote('echo %s > /dev/kmsg; insmod /data/local/tmp/n2de.ko' % mark)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.3)
subprocess.run([A, 'shell', 'am start -a android.media.action.STILL_IMAGE_CAMERA'], capture_output=True)
p.wait(); time.sleep(1)
out = n2d.sh('dmesg').decode(errors='replace').splitlines()
k = max([j for j, l in enumerate(out) if mark in l] or [0])
res = [l[l.find('N2D '):] for l in out[k:] if 'N2D ' in l]
open('out/early.log', 'w').write('\n'.join(res) + '\n'); print('\n'.join(res))
