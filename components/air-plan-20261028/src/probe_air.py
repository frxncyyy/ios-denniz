#!/usr/bin/env python3
"""Build/run a bounded donor AIR decoder against a local metal2vulkan checkout.

Only writes to research/. Apple libraries and diagnostic outputs stay untracked.
A successful decode does not prove translation, rendering, or Metal compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('libraries', nargs='+', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    work = args.work.resolve()
    if not work.is_relative_to(repo / 'research') or work.exists():
        parser.error('work must be a new directory beneath research/')
    dependency = args.translator.resolve() / 'crates/metal2vulkan-air'
    if not (dependency / 'Cargo.toml').is_file():
        parser.error('translator checkout has no metal2vulkan-air crate')
    libraries = [p.resolve(strict=True) for p in args.libraries]
    work.mkdir(parents=True)
    (work / 'src').mkdir()
    (work / 'src/main.rs').write_bytes(Path(__file__).with_name('air_probe.rs').read_bytes())
    (work / 'Cargo.toml').write_text(
        '[package]\nname="tegu-air-probe"\nversion="0.1.0"\nedition="2024"\n'
        '[dependencies]\nmetal2vulkan-air={path=' + json.dumps(str(dependency)) + '}\n')
    with (work / 'build.log').open('wb') as log:
        subprocess.run(['cargo', 'build', '--release', '--manifest-path', str(work / 'Cargo.toml')],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    results = []
    def limits():
        resource.setrlimit(resource.RLIMIT_AS, (500 * 1024**2, 500 * 1024**2))
    for index, library in enumerate(libraries):
        started = time.monotonic()
        # One isolated library attempt, 20 s total and <=500 MiB address space.
        try:
            run = subprocess.run([str(work / 'target/release/tegu-air-probe'), str(library)],
                                 capture_output=True, timeout=20, preexec_fn=limits)
            output = run.stdout + run.stderr
            status = run.returncode
        except subprocess.TimeoutExpired as exc:
            output = (exc.stdout or b'') + (exc.stderr or b'')
            status = 'timeout'
        (work / f'library-{index}.log').write_bytes(output)
        results.append(dict(library=str(library), sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                            elapsed_seconds=time.monotonic()-started, exit_status=status,
                            decoded=output.count(b'decode=OK'), refused=output.count(b'REFUSED')))
    report = dict(translator_commit=subprocess.check_output(
        ['git', '-C', str(args.translator), 'rev-parse', 'HEAD'], text=True).strip(), libraries=results)
    (work / 'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    return int(any(r['exit_status'] != 0 or r['refused'] or not r['decoded'] for r in results))


if __name__ == '__main__':
    raise SystemExit(main())
