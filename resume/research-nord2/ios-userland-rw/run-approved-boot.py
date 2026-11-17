# Run only after the human authorizes this exact boot23 image.
from pathlib import Path
import hashlib, subprocess, datetime, json, sys
assert sys.argv[1:] == ['--authorized-boot23'], 'Exact boot23 authorization required'
out = Path('research/nord2/ios-userland-rw')
img = Path('shim/out/nord2-xnu/nord2-ios-userland-stop23.img')
for p, size, digest in [
    (img, 33542144, '662285c21b8bcd2a5294b4be7175fce69b2d7b47e45aebf4286dd63434bd5c91'),
    (Path('backup/nord2/boot.img'), 33554432, '04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01'),
    (Path('research/nord2/rescue/nord2-adb-rescue.img'), 28884992, 'd78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'),
]:
    assert p.stat().st_size == size and hashlib.sha256(p.read_bytes()).hexdigest() == digest
ci = json.loads(Path('research/ci-39450476117/run.json').read_text())
assert ci['conclusion'] == 'success' and ci['headSha'] == '4e3f65181f0ac08dd1f524d0da405bf676c49c9b'
installed = json.loads((out/'install-bounded/result.json').read_text())
root_hash = '8b1d9de6775674afe790a161419ea23e824c5e045f21a66c6fc886125c991cb3'
assert installed['userdata_write_completed'] and installed['full_readback_verified']
assert installed['image_sha256'] == root_hash and installed['image_bytes'] == 17179869184
assert (out/'install-bounded/device-readback.sha256').read_text().split()[0] == root_hash
fb = '$HOME/Android/Sdk/platform-tools/fastboot'
serial = '<redacted-serial>'  # Private script; never commit.
r = subprocess.run([fb, 'devices'], capture_output=True, text=True, timeout=10, check=True)
assert [line.split() for line in r.stdout.splitlines() if line.strip()] == [[serial, 'fastboot']]
assert not (out/'hardware-live').exists()
with (out/'flash.log').open('x') as log:
    def run(*args):
        result = subprocess.run([fb, '-s', serial, *args], capture_output=True, text=True, timeout=120)
        text = result.stdout + result.stderr
        log.write(datetime.datetime.now().astimezone().isoformat()+' '+repr(args)+'\n'+text)
        log.flush()
        print(text, flush=True)
        result.check_returncode()
        return text
    assert 'product: oplus6893' in run('getvar', 'product')
    assert 'partition-size:boot: 2000000' in run('getvar', 'partition-size:boot')
    assert 'is-userspace: no' in run('getvar', 'is-userspace')
    run('flash', 'boot', str(img))
    with (out/'client-hardware.log').open('x') as clientlog:
        client = subprocess.Popen(['python3', 'tools/nord2-usb-console.py', str(out/'hardware-live'),
                                   '--wait', '300', '--read-seconds', '300'],
                                  stdout=clientlog, stderr=subprocess.STDOUT)
        (out/'client-hardware.pid').write_text(str(client.pid)+'\n')
        run('reboot')
        code = client.wait()
        print(json.dumps(dict(usb_client_exit=code)), flush=True)
        # No automatic reboot/restore: continuous userland may still be running.
        raise SystemExit(code)
