#!/usr/bin/env python3
"""Execute upstream authored Metal runtime fixtures on the host Vulkan GPU.

This is a host integration check, not Pixel GPU execution or donor equivalence.
Build the release compute_dispatch and render_to_texture examples first.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    work = args.work.resolve()
    if not work.is_relative_to(repo / 'research') or work.exists():
        parser.error('work must be a new directory beneath research/')
    translator = args.translator.resolve(strict=True)
    cases = ['compute_dispatch', 'render_to_texture']
    binaries = [translator / 'target/release/examples' / case for case in cases]
    if not all(binary.is_file() for binary in binaries):
        parser.error('build both release runtime examples first')
    work.mkdir(parents=True)
    results = []
    for case, binary in zip(cases, binaries):
        start = time.monotonic()
        try:
            run = subprocess.run([str(binary)], cwd=work, capture_output=True, timeout=90)
            output = run.stdout + run.stderr
            status = run.returncode
        except subprocess.TimeoutExpired as exc:
            output = (exc.stdout or b'') + (exc.stderr or b'')
            status = 'timeout'
        (work / (case + '.log')).write_bytes(output)
        results.append(dict(case=case, exit_status=status,
                            elapsed_seconds=time.monotonic() - start,
                            binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
        print(f'{case}: {status}\n{output.decode(errors="replace")}')
    report = dict(scope='Host Vulkan only; upstream authored fixtures, not donor shaders or Pixel',
                  translator_commit=subprocess.check_output(
                      ['git', '-C', str(translator), 'rev-parse', 'HEAD'], text=True).strip(),
                  tests=results)
    (work / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(result['exit_status'] != 0 for result in results))


if __name__ == '__main__':
    raise SystemExit(main())
