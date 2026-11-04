#!/usr/bin/env python3
"""Strict private link of Android board/clock/WA transport against prior kernel.

Does not replace approved images or send code to a remote builder.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir', type=Path, required=True)
args = p.parse_args()
OUT = args.output_dir.resolve()
OUT.mkdir(exist_ok=True)
KERNEL = OUT / 'kernel.development.nord2-metal-board'
assert not KERNEL.exists(), 'do not replace an existing kernel'


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


command = json.loads((ROOT / 'gpu/build/startup-diagnostic/link-command.json').read_text())
old_kernel = Path(command[command.index('-o') + 1])
assert sha(old_kernel) == '8b09d5aeb12ea53ea6beeb24e4552eff04bb8afae6b57fc7e2696b245c39ccce'
sources = {}
wa = ROOT / 'gpu/build/xnu-stage/bsd/dev/valhall_wa_firmware.h'
assert wa.read_bytes() == (ROOT / 'gpu/build/generated/valhall_wa_firmware.h').read_bytes()
wa_receipt = json.loads((ROOT / 'gpu/build/generated/valhall_wa_firmware.json').read_text())
assert sha(wa) == wa_receipt['generated_header_sha256']
(OUT / 'wa-receipt.json').write_text(json.dumps(wa_receipt, indent=2) + '\n')
for name in ['nord2_gpu_board.c', 'nord2_gpu_clk.c', 'nord2_gpu_hw.c', 'nord2_gpu_core.c', 'Nord2GPU.cpp']:
    cxx = name.endswith('.cpp')
    source = ROOT / 'gpu/build/xnu-stage' / ('iokit/Kernel/arm' if cxx else 'bsd/dev') / name
    assert source.read_bytes() == (ROOT / 'gpu/src' / name).read_bytes()
    obj = OUT / (source.stem + '.o')
    compile_command = ['python3', str(ROOT / 'gpu/tools/xnu_staged_compile.py'),
                       str(source), '--out', str(obj)] + (['--cxx'] if cxx else [])
    with (OUT / (source.stem + '-compile.log')).open('w') as log:
        subprocess.run(compile_command, cwd=ROOT, stdout=log, stderr=log, check=True)
    (OUT / name).write_bytes(source.read_bytes())
    sources[name] = {'source_sha256': sha(source), 'object_sha256': sha(obj),
                     'compile_command': compile_command}
    old_obj = str(ROOT / 'gpu/build/objs' / obj.name)
    assert command.count(old_obj) == 1
    command = [str(obj) if arg == old_obj else arg for arg in command]
command[command.index('-o') + 1] = str(KERNEL)
inputs = {arg: sha(Path(arg)) for arg in command[1:]
          if arg not in (str(KERNEL), '/dev/null') and Path(arg).is_file()}
with (OUT / 'link.log').open('w') as log:
    subprocess.run(command, cwd=ROOT, stdout=log, stderr=log, check=True)
assert all(sha(Path(path)) == digest for path, digest in inputs.items())
assert sha(old_kernel) == '8b09d5aeb12ea53ea6beeb24e4552eff04bb8afae6b57fc7e2696b245c39ccce'
(OUT / 'link-command.json').write_text(json.dumps(command, indent=2) + '\n')
record = {'kernel_sha256': sha(KERNEL), 'bytes': KERNEL.stat().st_size,
          'sources': sources, 'link_inputs': inputs, 'device_tested': False,
          'purpose': 'Android-derived cold PLL normalization and r0p1 dummy-job workaround'}
(OUT / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({k: v for k, v in record.items() if k not in ('sources', 'link_inputs')}, indent=2))
