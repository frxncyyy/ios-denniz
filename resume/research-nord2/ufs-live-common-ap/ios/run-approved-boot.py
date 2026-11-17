# Run only after the human authorizes the exact new Nord2 image recorded below.
from pathlib import Path
import hashlib, subprocess, datetime, json, sys
assert sys.argv[1:] == ['--authorized-live-x2-common-ap-boot'], 'Exact image authorization required'
out = Path('research/nord2/ufs-live-common-ap/ios')
img = Path('shim/out/nord2-xnu/nord2-ios-ufs2-wifi-ap-stop23.img')
for p, size, digest in [
    (img, 33277952, '3b2e7ecb2a8dfeecfda38615229aa6d9f5327a0ede2bfb6424ee355d14204434'),
    (Path('shim/out/nord2-xnu/nord2-ios-input-power-stop23.img'), 33544192, 'fd6d75caf4c7e95431bdb40eb50ca79650e8272824f54fd5f783cdec49995f20'),
    (Path('shim/out/nord2-xnu/nord2-ios-ufs-cost-stop23.img'), 33273856, 'ce84a4f313184c89973f65bd95e12dc3c23ac9fb17af4c58690d0ab9bc6dfb49'),
    (Path('shim/out/nord2-xnu/nord2-ios-wifi-common-stop23.img'), 33275904, '2e4f315e66bb84e9d811ca4245db08342ad1aff235e2b121fc7d13aea33dfd1d'),
    (Path('backup/nord2/boot.img'), 33554432, '04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01'),
    (Path('research/nord2/rescue/nord2-adb-rescue.img'), 28884992, 'd78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'),
]:
    assert p.stat().st_size == size and hashlib.sha256(p.read_bytes()).hexdigest() == digest
ci = json.loads(Path('research/ci-39510279443/run.json').read_text())
assert ci['conclusion'] == 'success' and ci['headSha'] == '4a8b957230c19db8969080778749a6e55e9e60f8'
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
                                   '--wait', '300', '--read-seconds', '180',
                                   '--timeout-ms', '5000', '--timeout-retries', '5'],
                                  stdout=clientlog, stderr=subprocess.STDOUT)
        (out/'client-hardware.pid').write_text(str(client.pid)+'\n')
        run('reboot')
        code = client.wait()
        print(json.dumps(dict(usb_client_exit=code)), flush=True)
        # No automatic reboot or rescue: live x2 permanently blocks Linux return.
        # Recovery requires physical reset to fastboot and a separately authorized image restore.
        raise SystemExit(code)
