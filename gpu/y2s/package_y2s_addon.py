#!/usr/bin/env python3
"""Assemble the y2sios-driver addon in the layout tools/stage_metal_root.py installs (bundle, probe, varying.bc).
Never touches a phone.  Usage: package_y2s_addon.py BUNDLE_DIR DONOR_ADDON.tar.gz OUT_DIR [--diag] [--remove-launchd-job /System/Library/LaunchDaemons/X.plist]...
(a removal is declared in the manifest as launchd_remove_jobs; update_metal_root.REMOVABLE_JOBS is the allow-list)"""
import gzip, hashlib, importlib.util, io, json, sys, tarfile, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[1]
bundle, donor, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
rest = sys.argv[4:]
diag = '--diag' in rest
if diag:
    rest = [x for x in rest if x != '--diag']
remove_jobs = [rest[i + 1] for i in range(0, len(rest), 2) if rest[i] == '--remove-launchd-job']
if len(rest) != 2 * len(remove_jobs):
    raise SystemExit('unknown argument; see usage')
out.mkdir(parents=True, exist_ok=True)
if (out / 'nord2-metal-addon.tar.gz').exists():
    raise SystemExit('refuse to replace an existing addon archive')
spec = importlib.util.spec_from_file_location('installer', root / 'tools/stage_metal_root.py')
inst = importlib.util.module_from_spec(spec); spec.loader.exec_module(inst)
with tempfile.TemporaryDirectory() as tmp:
    stage = Path(tmp)
    with tarfile.open(donor) as t:      # the probe and varying.bc are the donor's, unchanged
        for name in ('payload/nord2-metal-probe', 'payload/varying.bc'):
            (stage / Path(name).name).write_bytes(t.extractfile(name).read())
    (stage / 'Nord2Metal.bundle').mkdir()
    # the donor's PipelinePack stays: an update may not remove a path the installed addon owns (the y2sios driver ignores it)
    with tarfile.open(donor) as t:
        (stage / 'Nord2Metal.bundle/PipelinePack.tpcpack').write_bytes(t.extractfile('payload/Nord2Metal.bundle/PipelinePack.tpcpack').read())
    files = {}
    for f in sorted(bundle.iterdir()):
        data = f.read_bytes()
        (stage / 'Nord2Metal.bundle' / f.name).write_bytes(data)
    if diag:
        (stage / 'Nord2Metal.bundle' / 'diag').write_bytes(b'')       # the samplers and Metal-call timers run only with this marker present
    for f in sorted(stage.rglob('*')):
        if f.is_file():
            files[str(f.relative_to(stage))] = hashlib.sha256(f.read_bytes()).hexdigest()
    manifest = {'version': 1, 'files': files, 'hardware_executed': False, 'private_donor': True,
                'driver': 'y2sios redwoodmetal (MIT) over Nord2GPU, libredwoodair = Mesa 25.0.7 Panfrost + AIR frontend'}
    if remove_jobs:
        manifest['launchd_remove_jobs'] = remove_jobs
    (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    inst.validate_payload(stage)
    archive = out / 'nord2-metal-addon.tar.gz'
    with archive.open('wb') as raw, gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=0) as gz, tarfile.open(fileobj=gz, mode='w') as t:
        for name in [*sorted(files), 'manifest.json']:
            p = stage / name; data = p.read_bytes()
            info = tarfile.TarInfo('payload/' + name); info.size = len(data); info.uid = info.gid = 0; info.mtime = 0
            info.mode = 0o755 if p.name in ('Nord2Metal', 'nord2-metal-probe') else 0o644
            t.addfile(info, io.BytesIO(data))
        for p in (root / 'tools/stage_metal_root.py', root / 'tools/update_metal_root.py', root / 'docs/METAL_DEVICE_PLAN.md'):
            data = p.read_bytes(); info = tarfile.TarInfo(p.name); info.size = len(data); info.mode = 0o644; info.uid = info.gid = info.mtime = 0
            t.addfile(info, io.BytesIO(data))
receipt = {'path': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'bytes': archive.stat().st_size, 'payload': manifest}
(out / 'addon-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
