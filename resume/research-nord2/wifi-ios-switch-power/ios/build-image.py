"""Offline build from the exact successful CI kernel; never touches a device."""
from pathlib import Path
import argparse, hashlib, json, struct, subprocess, sys

P = Path(__file__).resolve().parent
R = next(p for p in P.parents if (p / 'AGENTS.md').is_file())
OLD = R / 'research/nord2/wifi-ios-live/ios'
IMAGE = R / 'shim/out/nord2-xnu/nord2-ios-wifi-switch-power-stop23.img'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def run(label, argv):
    with (P / (label + '.log')).open('xb') as out:
        subprocess.run(list(map(str, argv)), cwd=R, stdout=out, stderr=subprocess.STDOUT, check=True)

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--source', required=True)
ap.add_argument('--ci-run', type=int, required=True)
a = ap.parse_args()
ci = R / f'research/ci-{a.ci_run}'
record = json.loads((ci / 'run.json').read_text())
assert (record['status'], record['conclusion'], record['headSha'], record['databaseId']) == ('completed', 'success', a.source, a.ci_run)
subprocess.run(['git', 'merge-base', '--is-ancestor', a.source, 'HEAD'], cwd=R, check=True)
subprocess.run(['git', 'diff', '--exit-code', a.source, '--', 'shim', 'tools', 'xnu', '.github'], cwd=R, check=True, stdout=subprocess.DEVNULL)
assert not IMAGE.exists() and not (P / 'payload.bin').exists()
kernel = ci / 'xnu-kernel/kernel.development.sweet'
expected = [l.split()[0] for l in (kernel.parent / 'SHA256SUMS').read_text().splitlines() if len(l.split()) == 2 and Path(l.split()[1].lstrip('*')).name == kernel.name]
assert expected == [sha(kernel)]
baseline = json.loads((OLD / 'manifest.json').read_text())
base = R / 'research/nord2/rescue/recompressed/base.img'
assert sha(base) == baseline['base_sha256']
old_payload = (OLD / 'payload.bin').read_bytes()
assert hashlib.sha256(old_payload).hexdigest() == '96c717510604cb231f764d94bbc6f51cc0a650cb1ade485b23a28c4c69855562'
off, size = struct.unpack_from('<QQ', old_payload, 0x20)
adt = old_payload[off:off+size]
assert len(adt) == size and hashlib.sha256(adt).hexdigest() == baseline['private_calibration_adt_sha256']
cmdline = old_payload[0x48:0x48+608].split(b'\0')[0].decode()
assert cmdline == baseline['commandline'] and 'nord2wifilive=1' in cmdline.split()
(P / 'baseline.adt').open('xb').write(adt)
run('isa', ['python3', 'tools/nord2-check-rcpc2.py', kernel])
run('strip', [R / '../toolchains/cctools/bin/aarch64-apple-darwin-strip', '-S', '-x', kernel, '-o', P / 'kernel.strip'])
run('payload', ['python3', 'tools/mkpayload.py', P / 'kernel.strip', P / 'baseline.adt', P / 'payload.bin', '--cmdline', cmdline])
run('loader', ['sh', 'tools/nord2-build-loader.sh', P / 'loader'])
run('pack', ['python3', 'tools/nord2-pack-xnu.py', base, P / 'loader/loader-Image', P / 'payload.bin', IMAGE, '--zopfli', R / '../toolchains/zopfli/zopfli'])
run('check-image', ['python3', 'tools/nord2-check-xnu.py', IMAGE, P / 'payload.bin', '--stock', base])
assert 'FAILURES: none' in (P / 'check-image.log').read_text()
report = dict(source_commit=a.source, ci_run=a.ci_run, ci_attempt=record['attempt'], kernel_path=str(kernel.relative_to(R)), kernel_sha256=sha(kernel), image=str(IMAGE.relative_to(R)), image_bytes=IMAGE.stat().st_size, image_sha256=sha(IMAGE), payload_sha256=sha(P / 'payload.bin'), boot_spare_bytes=33554432-IMAGE.stat().st_size, baseline_adt_sha256=hashlib.sha256(adt).hexdigest(), commandline=cmdline, hardware_tested=False, dhcp_proven=False, exact_image_authorized=False, independent_image_verification_pending=True)
assert report['boot_spare_bytes'] >= 0
(P / 'build-result.json').open('x').write(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: report[k] for k in ('image', 'image_bytes', 'image_sha256', 'boot_spare_bytes')}))
