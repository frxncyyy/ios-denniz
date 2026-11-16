#!/usr/bin/env python3
"""Read only the fixed probe log from an offline image or verified RAM rescue.

No mounts, journal replay, disk writes, boot commands or full-volume hashes.
Dirty-volume output is an observation, not proof of catalog completeness.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import time

TARGET = 'private/var/db/nord2-metal/probe.log'
SIZE = 16 << 30


def module(path, digest, name):
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Pinned read-only dependency changed')
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--main', required=True, type=Path)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--image', type=Path)
    mode.add_argument('--adb', type=Path)
    ap.add_argument('--output', required=True, type=Path)
    ap.add_argument('--allow-dirty-observation', action='store_true')
    a = ap.parse_args()
    reader_mod = module(a.main / 'research/nord2/wifi/claude-opus-security-failure/review-log-access/collect.py',
        '00a7f8b414e0cf10e253c8d6ad2d6c828894867b4cd2a8e485405c51d1930801', 'metal_hfs_reader')
    a.output.mkdir(parents=True, exist_ok=False, mode=0o700)
    deadline = time.monotonic() + 120
    def bound():
        left = deadline - time.monotonic()
        if left <= 0:
            raise RuntimeError('Read deadline exceeded')
        return min(30, left)
    file = None
    try:
        if a.image:
            if a.image.stat().st_size != SIZE:
                raise RuntimeError('Image geometry mismatch')
            file = a.image.open('rb')
            def fetch(off, size):
                bound()
                file.seek(off)
                return file.read(size)
        else:
            pre = module(a.main / 'tools/nord2-install-root.py',
                '0addf55b16acc5c1476286cf09e147648b02d9360492f66ed12531018b867be1', 'metal_rescue_preflight')
            devices = subprocess.check_output([str(a.adb), 'devices'], text=True, timeout=bound()).splitlines()[1:]
            live = [s.split()[0] for s in devices if len(s.split()) == 2 and s.split()[1] in ('device', 'recovery')]
            if len(live) != 1:
                raise RuntimeError('Exactly one verified rescue required')
            serial = live[0]
            shell = pre.shell
            pre.shell = lambda adb, dev, cmd, timeout=30: shell(adb, dev, cmd, timeout=min(timeout, bound()))
            check = pre.preflight(str(a.adb), serial)
            (a.output / 'preflight.json').write_text(json.dumps(check, indent=2) + '\n')
            def fetch(off, size):
                if off < 0 or off % 4096 or size != 4096 or off + size > SIZE:
                    raise RuntimeError('Read transport bounds')
                cmd = f'dd if=/dev/block/sdc57 bs=4096 skip={off//4096} count=1 2>/dev/null'
                p = subprocess.run([str(a.adb), '-s', serial, 'shell', '-T', cmd],
                    capture_output=True, check=True, timeout=bound())
                if p.stderr or len(p.stdout) != size:
                    raise RuntimeError('Read transport status/length')
                return p.stdout
        reader = reader_mod.Reader(fetch, SIZE, a.allow_dirty_observation)
        info, raw = reader.read_log(TARGET)
        if not info.get('found'):
            raise RuntimeError('Probe log absent in this disk observation')
        # Reread every relevant page, without the cache: do not accept a file
        # assembled across different on-disk observations.
        for offset, page in reader.cache.items():
            if fetch(offset, 4096) != page:
                raise RuntimeError('Observed disk page changed')
        if a.adb:
            pre.preflight(str(a.adb), serial)
        (a.output / 'probe.log').write_bytes(raw)
        result = dict(file=info, writes=0, mounts=0, journal_replay=False,
            physical_bytes=reader.read_bytes*2, all_pages_reread_equal=True,
            volume_dirty=reader.dirty, trusted_clean_observation=not reader.dirty)
        (a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result))
    except Exception as exc:
        (a.output / 'failure.json').write_text(json.dumps(dict(error_type=type(exc).__name__, writes=0, mounts=0)) + '\n')
        raise SystemExit('Read-only collection refused; see failure.json') from None
    finally:
        if file:
            file.close()


if __name__ == '__main__':
    main()
