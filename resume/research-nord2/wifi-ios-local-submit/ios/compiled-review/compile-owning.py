#!/usr/bin/env python3
"""Actual generated ARM64 owning units; invoke only after root's source freeze.

Selected CI supplies command flags and headers. Current regenerated files are
snapshotted once, checked against canonical siblings, then compiled privately.
No generator, CI, image or device action. No output in older research stages.
"""
from pathlib import Path
import argparse, difflib, hashlib, json, re, shlex, shutil, subprocess, tarfile

P = Path(__file__).resolve().parent
R = next(p for p in P.parents if (p / 'AGENTS.md').exists())
CI = R / 'research/ci-39830672617'
BT = R / 'research/ci-39158169212/buildtree/xnu'
S = R / 'shim/nord2-xnu'
UNITS = {'wifi_station_transport': 'bsd/dev/wifi_station_transport.c', 'wifi_station_transaction': 'bsd/dev/wifi_station_transaction.c', 'wifi_wpa_nst': 'bsd/dev/wifi_wpa_nst.c', 'wifi_wpa_owner': 'bsd/dev/wifi_wpa_owner.c', 'wifi_wpa2': 'bsd/dev/wifi_wpa2.c', 'wifi_normal_transport': 'bsd/dev/wifi_normal_transport.c', 'wifi_normal_rx': 'bsd/dev/wifi_normal_rx.c', 'sweet_touch': 'bsd/dev/sweet_touch.c'}


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def dump(p, value):
    p.write_text(json.dumps(value, indent=2) + '\n')


def selected_commands():
    found = {}
    for line in (CI / 'build.log').read_text().splitlines():
        if ' -c ' not in line or '/Applications/' not in line:
            continue
        for name, relative in UNITS.items():
            if '/xnu/' + relative not in line:
                continue
            assert name not in found, name
            argv = shlex.split(line[line.index('/Applications/'):])
            assert '-mcpu=generic' in argv
            assert '-march=armv8.2-a+crypto' in argv
            found[name] = argv
    assert set(found) == set(UNITS)
    return found


def main():
    global P
    ap = argparse.ArgumentParser()
    ap.add_argument('--execute', action='store_true')
    ap.add_argument('--expected-patch', help='Root-confirmed regenerated0082 SHA256')
    ap.add_argument('--output', type=Path, help='Fresh attempt directory; never overwrite earlier result')
    args = ap.parse_args()
    if args.output:
        P = args.output.resolve()
        P.mkdir(parents=True, exist_ok=True)
    commands = selected_commands()
    plan = {
        'state': 'PREPARED_ONLY_UNTIL_EXPLICIT_EXECUTE',
        'selected_ci': '39830672617 attempt1',
        'ci_source': json.loads((CI / 'run.json').read_text()).get('headSha'),
        'selected_log_sha256': sha(CI / 'build.log'),
        'selected_archive_sha256': sha(CI / 'xnu-buildtree/buildtree.tgz'),
        'units': UNITS,
        'selected_commands': commands,
        'derived_new_unit': None,
        'local_changes': [
            'Apple compiler binary replaced by local Clang, version pinned.',
            '-arch arm64 replaced by -target arm64-apple-macos13.0.',
            'CI paths mapped to fresh private extraction/snapshot.',
            'Source-private osfmk include root added as in existing local focused compiler.',
            'Object and dependency outputs redirected; no warning-policy relaxation.',
            'Eight C units only: no Apple typed-allocation builtin substitutions are required or supplied.',
        ],
        'limitations': 'Compilation only; no linked, runtime, firmware, key or networking claim.',
    }
    if not args.execute:
        dump(P / 'plan.json', plan)
        print('PREPARED, not compiled:', ', '.join(UNITS))
        return
    patch = R / 'xnu/patches/0082-nord2-early-probe.patch'
    assert args.expected_patch and sha(patch) == args.expected_patch
    assert not (P / 'report.json').exists(), 'Never overwrite a completed review'
    inputs = P / 'inputs'
    assert not inputs.exists(), 'Use a fresh output stage'
    inputs.mkdir()
    archive = CI / 'xnu-buildtree/buildtree.tgz'
    # Exact saved CI inputs. No links are accepted and no path may escape.
    with tarfile.open(archive) as tf:
        members = tf.getmembers()
        assert all(not x.issym() and not x.islnk() for x in members)
        assert all((inputs / x.name).resolve().is_relative_to(inputs) for x in members)
        tf.extractall(inputs, members=members, filter='data')
    source_audit = json.loads((CI / 'source-buildtree-audit.json').read_text())
    assert source_audit['passed']
    generated = {}
    verified_siblings = {}
    differences = []
    current_rows = [{'member': 'xnu/' + x} for x in re.findall(r'^\+\+\+ b/(.*)$', patch.read_text(), re.M)]
    for row in current_rows:
        member = row['member']
        # Separate0083 applies after generator base; unused by these eight TUs.
        if member == 'xnu/iokit/Kernel/arm/SweetKeyStore.cpp':
            continue
        if not member.startswith('xnu/'):
            continue
        relative = member[4:]
        source = BT / relative
        before = inputs / member
        current = source.read_bytes()
        canonical = S / source.name
        if canonical.is_file():
            body = canonical.read_bytes()
            guarded = b'#include <arm64/sweet_early.h>\n#if defined(NORD2)\n' + body + b'\n#endif /* NORD2 */\n'
            assert current in (body, guarded), 'Generated/canonical mismatch: ' + relative
            verified_siblings[str(canonical.relative_to(R))] = sha(canonical)
        generated[relative] = sha(source)
        if not before.exists() or current != before.read_bytes():
            old = before.read_text().splitlines(keepends=True) if before.exists() else []
            new = current.decode().splitlines(keepends=True)
            differences.extend(difflib.unified_diff(old, new, 'selected-ci/' + relative, 'current/' + relative))
        before.write_bytes(current)
    (P / 'selected-ci-differential.patch').write_text(''.join(differences))
    assert 'wifi_station_service_bind.inc' in '\n'.join(generated)
    dump(P / 'source-pins.json', {'patch_sha256': args.expected_patch, 'generated': generated,
                                'canonical_siblings': verified_siblings})
    records = []
    prefix = '/Users/runner/work/ios-sweet/ios-sweet/dxb/'
    sdk = '/Applications/Xcode_15.0.1.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX14.0.sdk//System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/platform'
    for name, relative in UNITS.items():
        original = commands[name]
        cpp = relative.endswith('.cpp')
        cwd = inputs / ('build/xnu.obj/DEVELOPMENT_ARM64_SWEET/' + ('iokit' if cpp else 'bsd') + '/DEVELOPMENT')
        argv = [shutil.which('clang++' if cpp else 'clang'), '-target', 'arm64-apple-macos13.0',
                '-I' + str(inputs / 'xnu/osfmk')]
        assert not cpp, 'This bounded review owns only eight C units'
        i = 1
        while i < len(original):
            a = original[i]
            if a in ('-arch', '-o', '-MF'):
                value = original[i + 1]
                if a == '-arch':
                    assert value == 'arm64'
                elif a == '-o':
                    argv.extend(['-o', str(P / (name + '.o'))])
                else:
                    argv.extend(['-MF', str(P / (name + '.d'))])
                i += 2
                continue
            a = a.replace(prefix, str(inputs) + '/').replace(sdk, str(inputs / 'sdk-kernel-platform'))
            argv.append(a)
            i += 1
        if '-o' not in argv:
            argv.extend(['-o', str(P / (name + '.o'))])
        assert argv.count('-c') == 1
        assert '-mcpu=generic' in argv and '-march=armv8.2-a+crypto' in argv
        result = subprocess.run(argv, cwd=cwd, capture_output=True)
        (P / (name + '.log')).write_bytes(result.stdout + result.stderr)
        record = {'name': name, 'argv': argv, 'cwd': str(cwd), 'exit': result.returncode,
                  'source': relative, 'source_sha256': generated[relative]}
        if not result.returncode:
            record['object_sha256'] = sha(P / (name + '.o'))
            deps = (P / (name + '.d')).read_text().replace('\\\n', ' ').split(':', 1)[1].split()
            record['dependencies'] = {}
            for dependency in deps:
                dep = Path(dependency)
                if not dep.is_absolute():
                    dep = cwd / dep
                if dep.is_file():
                    record['dependencies'][str(dep.resolve())] = sha(dep)
        records.append(record)
        print(name, result.returncode, flush=True)
        if result.returncode:
            print((result.stdout + result.stderr).decode(errors='replace')[-5000:])
            break
    # A concurrent canonical/generator change invalidates the result.
    assert sha(patch) == args.expected_patch
    assert all(sha(BT / p) == h for p, h in generated.items())
    assert all(sha(R / p) == h for p, h in verified_siblings.items())
    passed = len(records) == len(UNITS) and all(not x['exit'] for x in records)
    dump(P / 'report.json', {'passed': passed, 'plan': plan, 'commands': records,
                            'compiler': subprocess.check_output(['clang', '--version'], text=True),
                            'source_pins_sha256': sha(P / 'source-pins.json')})
    assert passed


if __name__ == '__main__':
    main()
