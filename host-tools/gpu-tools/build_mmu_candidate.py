#!/usr/bin/env python3
"""Relink the verified Android-init kernel with explicit GPU-local sharing.
Private local build only. Never overwrites an approved kernel or touches USB.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
out = a.output_dir.resolve()
out.mkdir(exist_ok=True)
kernel = out/'kernel.development.nord2-metal-shared'
assert not kernel.exists(), 'refuse to overwrite a kernel'

def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()

baseline = ROOT/'gpu/build/board-wa-candidate'
receipt = json.loads((baseline/'receipt.json').read_text())
command = json.loads((baseline/'link-command.json').read_text())
old_kernel = Path(command[command.index('-o')+1])
assert sha(old_kernel) == receipt['kernel_sha256'] == '3f72ba61899f080486ec545dc11786d8c0fab84ba443e6a65117461b7b06ee72'
for file, digest in receipt['link_inputs'].items():
    assert sha(file) == digest, 'baseline link input changed: '+file
sources = {}
for name in ['nord2_gpu_mmu.c','nord2_gpu_core.c','Nord2GPU.cpp']:
    cxx = name.endswith('.cpp')
    src = ROOT/'gpu/build/xnu-stage'/('iokit/Kernel/arm' if cxx else 'bsd/dev')/name
    assert src.read_bytes() == (ROOT/'gpu/src'/name).read_bytes()
    obj = out/(src.stem+'.o')
    assert not obj.exists(), 'refuse to replace a compiled object'
    argv = ['python3',str(ROOT/'gpu/tools/xnu_staged_compile.py'),str(src),'--out',str(obj)] + (['--cxx'] if cxx else [])
    with (out/(src.stem+'-compile.log')).open('w') as log:
        subprocess.run(argv,cwd=ROOT,stdout=log,stderr=log,check=True)
    candidates = [x for x in command if Path(x).name == obj.name]
    assert len(candidates) == 1
    original = candidates[0]
    command = [str(obj) if x == original else x for x in command]
    (out/name).write_bytes(src.read_bytes())
    sources[name] = {'source_sha256':sha(src),'object_sha256':sha(obj),'replaces':original,'compile_command':argv}
command[command.index('-o')+1] = str(kernel)
inputs = {x:sha(x) for x in command[1:] if x not in (str(kernel),'/dev/null') and Path(x).is_file()}
headers = {str(x.relative_to(ROOT)):sha(x) for x in (ROOT/'gpu/build/xnu-stage/bsd/dev').glob('nord2_gpu_*.h')}
with (out/'link.log').open('w') as log:
    subprocess.run(command,cwd=ROOT,stdout=log,stderr=log,check=True)
for file,digest in inputs.items():
    assert sha(file) == digest
for file,digest in receipt['link_inputs'].items():
    assert sha(file) == digest
assert sha(old_kernel) == receipt['kernel_sha256']
(out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
record = {'kernel_sha256':sha(kernel),'bytes':kernel.stat().st_size,'sources':sources,'headers':headers,'link_inputs':inputs,'baseline_kernel_sha256':receipt['kernel_sha256'],'device_tested':False,'purpose':'GPU inner-shareable default-cache BO leaves, vendor WA unchanged; raw leaf diagnostics'}
(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps({k:v for k,v in record.items() if k not in ('sources','headers','link_inputs')},indent=2))
