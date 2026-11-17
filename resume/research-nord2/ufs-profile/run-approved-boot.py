# Run only after the human authorizes the exact Nord2 UFS profile/console correction image.
from pathlib import Path
import hashlib, subprocess, datetime, json, sys
assert sys.argv[1:] == ['--authorized-ufs-profile-boot'], 'Exact UFS profile/console correction boot authorization required'
out = Path('research/nord2/ufs-profile')
img = Path('shim/out/nord2-xnu/nord2-ios-ufs-profile-fastlog-stop23.img')
for p, size, digest in [
    (img, 33550336, '8eb3f47705826255fb8841327ce8dd504caf7b5f90e89eb05f6c2864ec01dfe8'),
    (Path('shim/out/nord2-xnu/nord2-ios-input-power-stop23.img'), 33544192, 'fd6d75caf4c7e95431bdb40eb50ca79650e8272824f54fd5f783cdec49995f20'),
    (Path('backup/nord2/boot.img'), 33554432, '04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01'),
    (Path('research/nord2/rescue/nord2-adb-rescue.img'), 28884992, 'd78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'),
]:
    assert p.stat().st_size == size and hashlib.sha256(p.read_bytes()).hexdigest() == digest
ci = json.loads(Path('research/ci-39488497958/run.json').read_text())
assert ci['conclusion'] == 'success' and ci['headSha'] == '242e41fb89fd72a58f593676c6cd776e30c4ff51'
installed = json.loads(Path('research/nord2/ios-userland-rw/install-bounded/result.json').read_text())
root_hash = '8b1d9de6775674afe790a161419ea23e824c5e045f21a66c6fc886125c991cb3'
assert installed['userdata_write_completed'] and installed['full_readback_verified']
assert installed['image_sha256'] == root_hash and installed['image_bytes'] == 17179869184
assert Path('research/nord2/ios-userland-rw/install-bounded/device-readback.sha256').read_text().split()[0] == root_hash
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
                                   '--wait', '300', '--read-seconds', '600',
                                   '--timeout-ms', '5000', '--timeout-retries', '5'],
                                  stdout=clientlog, stderr=subprocess.STDOUT)
        (out/'client-hardware.pid').write_text(str(client.pid)+'\n')
        run('reboot')
        code = client.wait()
        print(json.dumps(dict(usb_client_exit=code)), flush=True)
        # No automatic reboot/restore: continuous userland may still be running.
        raise SystemExit(code)
