# watch.py <sv_index> <seconds> <cg_bit> <tag>: start watcher, then open main camera from closed state
import sys, os, subprocess, time, shlex
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import n2d
sv = 0x1a090000 + 0x1000 * int(sys.argv[1]); secs = float(sys.argv[2]); cg = int(sys.argv[3]); tag = sys.argv[4]
ko = bytearray(open('n2d.ko', 'rb').read()); i = ko.index(b'N2DOPS01')
b = n2d.build([os.environ["WOP"]] if "WOP" in os.environ else [f"WATCH {sv:#x} {int(secs * 13e6)} {cg}"]); ko[i:i + len(b)] = b
open('n2d_run.ko', 'wb').write(ko)
A = n2d.ADB
subprocess.run([A, 'push', 'n2d_run.ko', '/data/local/tmp/n2dw.ko'], capture_output=True)
subprocess.run([A, 'shell', 'am force-stop com.oplus.camera; input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard'], capture_output=True)
time.sleep(3)
mark = 'n2dwatch%d' % time.time()
p = subprocess.Popen([A, 'shell', 'su -c ' + shlex.quote('echo %s > /dev/kmsg; insmod /data/local/tmp/n2dw.ko' % mark)])
time.sleep(0.5)
subprocess.run([A, 'shell', os.environ.get('CAMCMD', 'am start -a android.media.action.STILL_IMAGE_CAMERA')], capture_output=True)
p.wait(); time.sleep(1)
out = n2d.sh('dmesg').decode(errors='replace').splitlines()
k = max([j for j, l in enumerate(out) if mark in l] or [0])
res = [l[l.find('N2D '):] for l in out[k:] if 'N2D ' in l]
open('out/watch_%s.log' % tag, 'w').write('\n'.join(res) + '\n')
print(len(res), 'lines')
