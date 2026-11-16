#!/bin/sh
# Build a reproducible local candidate package and manifest.  Nothing is
# installed, uploaded, or sent to the phone.
set -eu
cd "$(dirname "$0")/../.."
WORK=$PWD
OUT=$WORK/gpu/out
STAGE=$OUT/nord2-gpu-candidate
rm -rf "$STAGE"
mkdir -p "$STAGE"/{kernel,src,docs,artifacts,tests,tools}

./gpu/tools/regenerate_patch.sh
./gpu/tools/run_host_tests.sh > "$OUT/host-tests.log" 2>&1

cp xnu/patches/0084-nord2-gpu.patch "$STAGE/kernel/"
cp shim/nord2-xnu/Nord2GPU.cpp shim/nord2-xnu/nord2_gpu_* "$STAGE/kernel/"
cp gpu/src/*.c gpu/src/*.cpp "$STAGE/src/" 2>/dev/null || true
cp gpu/include/*.h "$STAGE/src/"
cp gpu/docs/*.md "$STAGE/docs/"
cp gpu/tests/*.py gpu/tests/*.c "$STAGE/tests/"
cp -r gpu/tests/shaders "$STAGE/tests/"
cp -r gpu/metal "$STAGE/src/"
cp tools/mkadt-nord2.py tools/test-nord2-adt-gpu.py tools/adt.py "$STAGE/tools/"
cp gpu/tools/*.py gpu/tools/*.sh gpu/tools/nord2_mali_compile.c "$STAGE/src/"
cp gpu/build/compiler-test/token.bin gpu/build/compiler-test/token.dis \
   gpu/build/compiler-test/summary.json "$STAGE/artifacts/" 2>/dev/null || true

python3 - "$STAGE" <<'PY'
import hashlib, json, os, subprocess, sys, time
stage = sys.argv[1]
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(stage))))
def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()
def ver(cmd):
    try:
        return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT).splitlines()[0]
    except Exception as e:
        return f'unavailable: {e}'
files = {}
for base, _, names in os.walk(stage):
    for n in sorted(names):
        p = os.path.join(base, n)
        files[os.path.relpath(p, stage)] = sha(p)
summary = {}
try:
    with open(os.path.join(root, 'gpu/build/compiler-test/summary.json')) as f:
        summary = json.load(f)
except Exception:
    pass
manifest = {
    'candidate': os.path.basename(root),
    'branch': 'deepseek/nord2-metal',
    'baseline': 'b2ccaed',
    'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
    'references': {
        'ios-tegu': '7f6f7ca9c58ab840cb1a9c306e7cba3bddd2d572',
        'mesa': '81daea66703eeea1a232446e3a36902353fc3045',
        'metal2vulkan-tegu-pin': '720ef1ce9ae2a2068999a8675717418b9416283e',
        'android-kernel': '04d00c2565764c3922c9b4826b5acff1106e533f',
    },
    'toolchain': {
        'clang': ver(['clang', '--version']),
        'meson': ver(['meson', '--version']),
        'python': ver(['python3', '--version']),
    },
    'arch9_compiler': summary,
    'status': {
        'host_tests': 'pass',
        'device_tested': False,
        'gpu_id_verified': False,
        'metal_frontend': False,
        'presentation': False,
        'power_engine': False,
        'default_enabled': False,
    },
    'files': files,
}
with open(os.path.join(stage, 'manifest.json'), 'w') as f:
    json.dump(manifest, f, indent=2)
    f.write('\n')
with open(os.path.join(stage, 'SHA256SUMS'), 'w') as f:
    for rel in sorted(files):
        f.write(f'{files[rel]}  {rel}\n')
PY

tar -C "$OUT" -czf "$OUT/nord2-gpu-candidate.tar.gz" nord2-gpu-candidate
sha256sum "$OUT/nord2-gpu-candidate.tar.gz" > "$OUT/nord2-gpu-candidate.tar.gz.sha256"
cat "$OUT/nord2-gpu-candidate.tar.gz.sha256"
