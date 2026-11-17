#!/usr/bin/env python3
"""Private candidate verification for the inherited-state audio startup fix.

Copies the repo's shim/nord2-xnu, tests/ and tools/nord2-audio-*.py into a
temporary tree, overlays every file under this candidate/ directory at the
same relative path, then:
  1. runs the (candidate) tools/nord2-audio-check.py --board <pinned cnt>,
     i.e. the whole existing audio suite plus test-inherited cases;
  2. builds tests/nord2-audio/plan-dump.c against CANONICAL and CANDIDATE
     audio_afe.{c,h} and requires byte-identical cold-fixture programmes,
     canonical refusal of the real snapshot (gate f8) and the candidate's
     48/34-op real-snapshot programme;
  3. writes a receipt under candidate/../verification/<UTC stamp>/.
No canonical edits, git, CI, device, network or flash. Model results are not
hardware proof.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, shutil, subprocess, tempfile

C = Path(__file__).resolve().parent
PIN = '635300fbc96f258ff9a35f82435f2e9d3ff6f1370c156067b40146e011038692'
FLAGS = ['clang', '-std=c11', '-DNORD2', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-misleading-indentation',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', type=Path, default=C.parents[5])
    ap.add_argument('--board', type=Path)
    a = ap.parse_args()
    repo = a.repo.resolve()
    board = (a.board or repo / 'research/nord2/audio/playback-20261023/profile-sol/tfa98xx.cnt').resolve()
    assert sha(board) == PIN, 'private board container hash mismatch'
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out = C.parent / 'verification' / stamp
    out.mkdir(parents=True)
    log = []

    def run(cmd, **kw):
        r = subprocess.run([str(x) for x in cmd], text=True, capture_output=True, **kw)
        log.append(dict(command=[str(x) for x in cmd], exit=r.returncode, stdout=r.stdout, stderr=r.stderr))
        (out / 'results.json').write_text(json.dumps(log, indent=2))
        print(r.stdout, end=''); print(r.stderr, end='')
        if r.returncode:
            raise SystemExit(r.returncode)
        return r.stdout

    overlay = sorted(p for p in C.rglob('*') if p.is_file() and p.name != 'run-candidate.py'
                     and '__pycache__' not in p.parts)
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp) / 'tree'
        skip = shutil.ignore_patterns('__pycache__')
        shutil.copytree(repo / 'shim/nord2-xnu', tree / 'shim/nord2-xnu', ignore=skip)
        shutil.copytree(repo / 'tests', tree / 'tests', ignore=skip)
        (tree / 'tools').mkdir()
        for f in sorted((repo / 'tools').glob('nord2-audio-*.py')):
            shutil.copy2(f, tree / 'tools' / f.name)
        # The audio CLI imports the shared USB transport from the Wi-Fi CLI.
        shutil.copy2(repo / 'tools/nord2-wifi-live.py', tree / 'tools/nord2-wifi-live.py')
        for p in overlay:
            dst = tree / p.relative_to(C)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dst)

        run(['python3', '-I', tree / 'tools/nord2-audio-check.py', '--board', board])

        def dump(src):
            exe = Path(tmp) / ('plan-dump-' + ('canonical' if src == repo / 'shim/nord2-xnu' else 'candidate'))
            run(FLAGS + ['-I' + str(src), '-I' + str(tree / 'tests/nord2-audio'),
                         tree / 'tests/nord2-audio/plan-dump.c', src / 'audio_afe.c', src / 'audio_id.c',
                         '-o', exe])
            return run([exe]).splitlines()
        canonical = dump(repo / 'shim/nord2-xnu')
        candidate = dump(tree / 'shim/nord2-xnu')
        (out / 'plan-canonical.txt').write_text('\n'.join(canonical) + '\n')
        (out / 'plan-candidate.txt').write_text('\n'.join(candidate) + '\n')
        for prefix in ('cold ', 'cold-setup '):
            can = [l for l in canonical if l.startswith(prefix)]
            cand = [l for l in candidate if l.startswith(prefix)]
            assert can and can == cand, 'cold programme changed: ' + prefix
        assert 'actual refused gate=f8' in canonical, 'canonical baseline not reproduced'
        assert 'actual gate=0 up=48 down=34' in candidate, 'candidate real-snapshot programme'

    receipt = dict(stamp=stamp, repo=str(repo), board_sha256=PIN,
                   canonical={str(p.relative_to(repo)): sha(p) for p in
                              sorted((repo / 'shim/nord2-xnu').glob('audio_*')) if p.is_file()},
                   candidate={str(p.relative_to(C)): sha(p) for p in overlay},
                   result='PASS host models only; physical sound and real I2C/amp state unverified')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS candidate suite + cold-plan identity; receipt', out)


if __name__ == '__main__':
    main()
