# Private, fail-closed template. Parent alone may prepare/use after exact human approval.
from pathlib import Path
import hashlib, subprocess, datetime, json, sys, importlib.util, os
assert sys.argv[1:] == ['--authorized-wifi-wfsys-boot'], 'Exact new image authorization required'
out=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('verify',out/'verify-image.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
cfg=v.load_identity()
os.chdir(v.ROOT)
manifest=json.loads((out/'manifest.json').read_text())
ready=json.loads((out/'prepared-image-required.json').read_text())
authorization=json.loads((out/'authorization-required.json').read_text())
assert ready['parent_verified'] is True and authorization['human_exact_image_authorized'] is True
assert authorization['image_sha256']==ready['image_sha256']==manifest['image_sha256']
assert authorization['device_serial'] and authorization['scope']=='boot-only'
assert manifest['ci_run']==cfg['ci_run'] and manifest['expected_kernel_source']==cfg['source_commit']
assert manifest['commandline']==v.COMMANDLINE and manifest['kernel_sha256']==cfg['kernel_sha256']
img=v.IMAGE
for path,size,digest in [(img,ready['image_bytes'],ready['image_sha256']),
 (v.ROOT/'shim/out/nord2-xnu/nord2-ios-wifi-ap-wake-stop23.img',33292288,'49f177ec5d977d4f7c0d1407c78f585e5704ccc85c1cf17804f6c940ad8e9310'),
 (v.ROOT/'backup/nord2/boot.img',33554432,'04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01'),
 (v.ROOT/'research/nord2/rescue/nord2-adb-rescue.img',28884992,'d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5')]:
 assert isinstance(digest,str) and len(digest)==64 and size>0
 assert path.stat().st_size==size and hashlib.sha256(path.read_bytes()).hexdigest()==digest
subprocess.run([sys.executable,str(out/'verify-image.py'),'--preflight'],check=True)
installed = json.loads(Path('research/nord2/ios-userland-rw/install-bounded/result.json').read_text())
root_hash = '8b1d9de6775674afe790a161419ea23e824c5e045f21a66c6fc886125c991cb3'
assert installed['userdata_write_completed'] and installed['full_readback_verified']
assert installed['image_sha256'] == root_hash and installed['image_bytes'] == 17179869184
assert Path('research/nord2/ios-userland-rw/install-bounded/device-readback.sha256').read_text().split()[0] == root_hash
fb = '$HOME/Android/Sdk/platform-tools/fastboot'
serial = authorization['device_serial']  # Supplied privately only after exact approval.
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
        # Recovery requires physical reset to fastboot and separately recorded authorization for the selected image restore.
        raise SystemExit(code)
