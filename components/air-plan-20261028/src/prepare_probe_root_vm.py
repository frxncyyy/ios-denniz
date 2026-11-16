#!/usr/bin/env python3
"""Clone the verified Setup-skipped root and install the Metal probe in KVM."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_gui_root_vm import digest
from verify_root_vm import entries, replace_init


def guest_readback_python(offscreen_ui, target_root='/target', probe_root='/probe', require_root_owner=True,
                          null_tee_stdio=False):
    return f'''import copy, hashlib, json, plistlib
from pathlib import Path
OFFSCREEN_UI={offscreen_ui!r}
NULL_TEE_STDIO={null_tee_stdio!r}
REQUIRE_ROOT_OWNER={require_root_owner!r}
root=Path({target_root!r})
probe_root=Path({probe_root!r})
probe=root/'usr/local/bin/tegu-metal-probe'
assert probe.read_bytes()==(probe_root/'tegu-metal-probe').read_bytes()
st=probe.stat()
assert st.st_mode&0o777==0o755
if REQUIRE_ROOT_OWNER: assert (st.st_uid,st.st_gid)==(0,0)
prefs=plistlib.loads((root/'private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist').read_bytes())
assert prefs['SetupFinishedAllSteps'] is True
config=root/'System/Library/xpc/launchd.plist'
original_path=config.with_name(config.name+'.tegu-metal-original')
original_bytes=original_path.read_bytes()
original=plistlib.loads(original_bytes)
current_bytes=config.read_bytes()
current=plistlib.loads(current_bytes)
expected=copy.deepcopy(original)
daemons=expected['LaunchDaemons']
if OFFSCREEN_UI:
    manifest_path=root/'var/db/tegu-compositor-offscreen-ui.json'
    manifest=json.loads(manifest_path.read_text())
    assert manifest['schema']==1 and manifest['mode']=='offscreen-ui-diagnostic'
    assert manifest['source_launchd_sha256']==hashlib.sha256(original_bytes).hexdigest()
    assert manifest['installed_launchd_sha256']==hashlib.sha256(current_bytes).hexdigest()
    assert set(manifest['disabled_jobs'])=={{'com.apple.backboardd','com.apple.SpringBoard'}}
    for label,entry in manifest['disabled_jobs'].items():
        matches=[(key,job) for key,job in daemons.items() if job.get('Label')==label]
        assert len(matches)==1 and matches[0][0]==entry['plist_key']
        before=matches[0][1]
        assert hashlib.sha256(plistlib.dumps(before,fmt=plistlib.FMT_BINARY,sort_keys=True)).hexdigest()==entry['original_job_sha256']
        after=copy.deepcopy(before);after['Disabled']=True;daemons[entry['plist_key']]=after
        installed=[job for job in current['LaunchDaemons'].values() if job.get('Label')==label]
        assert len(installed)==1 and installed[0].get('Disabled') is True
        assert hashlib.sha256(plistlib.dumps(installed[0],fmt=plistlib.FMT_BINARY,sort_keys=True)).hexdigest()==entry['installed_job_sha256']
    print('TEGU_METAL_OFFSCREEN_MANIFEST_OK sha256='+hashlib.sha256(manifest_path.read_bytes()).hexdigest())
else:
    assert not (root/'var/db/tegu-compositor-offscreen-ui.json').exists()
current_daemons=current['LaunchDaemons']
probe_key='/System/Library/LaunchDaemons/org.tegu.metal-discovery.plist'
assert probe_key not in daemons
stdio_path='/dev/null' if NULL_TEE_STDIO else '/dev/console'
daemons[probe_key]={{'Label':'org.tegu.metal-discovery','ProgramArguments':['/usr/local/bin/tegu-metal-probe', *json.loads((probe_root/'probe-arguments.json').read_text())],
 'RunAtLoad':True,'UserName':'root','StandardOutPath':stdio_path,'StandardErrorPath':stdio_path}}
assert current==expected, 'Unexpected launchd changes beyond probe registration and optional two-job diagnostic'
job=current_daemons[probe_key]
assert job['ProgramArguments']==['/usr/local/bin/tegu-metal-probe', *json.loads((probe_root/'probe-arguments.json').read_text())]
assert job['RunAtLoad'] is True and not job.get('KeepAlive')
assert job['StandardOutPath']==stdio_path and job['StandardErrorPath']==stdio_path
if NULL_TEE_STDIO: print('TEGU_METAL_NULL_TEE_STDIO_READBACK_OK')
if not OFFSCREEN_UI:
    print('TEGU_METAL_UI_JOBS_UNCHANGED_OK')
print('TEGU_METAL_ROOT_READBACK_OK')
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'source-manifest', 'work', 'output', 'fsck', 'probe'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--bundle', type=Path, help='Optional experimental Metal plugin')
    parser.add_argument('--probe-argument', action='append', default=[],
                        help='One literal launchd argv element for the probe (repeatable; no shell parsing)')
    parser.add_argument('--offscreen-ui', action='store_true',
                        help='Optional diagnostic clone: disable only the backboardd and SpringBoard launchd jobs')
    parser.add_argument('--null-tee-stdio', action='store_true',
                        help='Optional diagnostic clone: route only probe stdout/stderr to /dev/null for SWEET null tee')
    args = parser.parse_args()
    if any('\0' in value for value in args.probe_argument):
        parser.error('probe arguments must be NUL-free strings')
    build, work, output = args.build.resolve(), args.work.resolve(), args.output.resolve()
    result = json.loads(args.source_manifest.read_text())
    source = Path(result['image'])
    if not all(result.get(k) for k in ('fsck_verified', 'setup_skip_verified', 'full_bringup_verified')):
        raise SystemExit('Require verified Setup-skipped source')
    if output.exists() or ',' in str(output) or ',' in str(work):
        raise SystemExit('Require new output and QEMU-safe paths')
    work.mkdir(parents=True, exist_ok=False)
    subprocess.run(['cp', '--reflink=always', '--sparse=auto', '--', str(source), str(output)], check=True)
    if digest(output) != result['sha256']:
        raise SystemExit('Source clone hash mismatch')
    shutil.copyfile(args.probe, work / 'tegu-metal-probe')
    if args.bundle:
        shutil.copytree(args.bundle, work / 'TeguMetal.bundle', symlinks=True)
    shutil.copyfile(Path(__file__).with_name('install_native_probe.py'), work / 'install.py')
    (work / 'probe-arguments.json').write_text(json.dumps(args.probe_argument, ensure_ascii=False) + '\n')
    archive = gzip.decompress((build / 'initramfs.cpio.gz').read_bytes())
    init = next(data for _, name, data in entries(archive) if name == b'init\0')
    prelude = init[:init.index(b'for source in os systemos appos;')].decode()
    prelude = prelude.replace('TEGU_IOS_ROOT_FAILED', 'TEGU_METAL_ROOT_FAILED')
    script = prelude + r'''
mkdir -p /hostusr /probe
mount -t 9p -o trans=virtio,version=9p2000.L,ro hostusr /hostusr
mount -t 9p -o trans=virtio,version=9p2000.L,ro probe /probe
mount -t hfsplus -o force /dev/vda /target
export PYTHONHOME=/hostusr
/hostusr/lib/ld-linux-x86-64.so.2 --library-path /hostusr/lib /hostusr/bin/python3 -S /probe/install.py /target /probe/tegu-metal-probe --probe-arguments-json /probe/probe-arguments.json__OFFSCREEN_ARG__
sync
umount /target
mount -t hfsplus -o ro /dev/vda /target
/hostusr/lib/ld-linux-x86-64.so.2 --library-path /hostusr/lib /hostusr/bin/python3 -S - <<'VERIFY'
__VERIFY_SOURCE__
VERIFY
umount /target
echo TEGU_METAL_ROOT_OK
trap - EXIT
poweroff -f
'''
    script = script.replace('__OFFSCREEN_ARG__',
        (' --offscreen-ui' if args.offscreen_ui else '') +
        (' --null-tee-stdio' if args.null_tee_stdio else ''))
    script = script.replace('__VERIFY_SOURCE__', guest_readback_python(args.offscreen_ui,
        null_tee_stdio=args.null_tee_stdio))
    if args.bundle:
        installer_prefix='/probe/install.py /target /probe/tegu-metal-probe --probe-arguments-json /probe/probe-arguments.json'
        command_lines=[line for line in script.splitlines() if installer_prefix in line]
        if len(command_lines)!=1 or command_lines[0].count(installer_prefix)!=1:
            raise SystemExit('Guest installer command missing or ambiguous')
        script = script.replace(command_lines[0],command_lines[0]+' --bundle /probe/TeguMetal.bundle')
        script = script.replace("print('TEGU_METAL_ROOT_READBACK_OK')", """
bundle=root/'System/Library/Extensions/TeguMetal.bundle'
for source in Path('/probe/TeguMetal.bundle').rglob('*'):
    target=bundle/source.relative_to('/probe/TeguMetal.bundle')
    if source.is_file():
        assert target.read_bytes()==source.read_bytes()
        st=target.stat()
        assert (st.st_uid,st.st_gid)==(0,0)
assert (bundle/'TeguMetal').stat().st_mode&0o777==0o755
print('TEGU_METAL_BUNDLE_READBACK_OK')
print('TEGU_METAL_ROOT_READBACK_OK')
""")
    (work / 'initramfs.cpio.gz').write_bytes(gzip.compress(replace_init(archive, script.encode())))
    command = json.loads((build / 'command.json').read_text())
    while '-virtfs' in command:
        i = command.index('-virtfs'); del command[i:i+2]
    command[command.index('-initrd')+1] = str(work / 'initramfs.cpio.gz')
    command[command.index('-drive')+1] = f'file={output},format=raw,if=virtio'
    command += ['-virtfs', 'local,path=/usr,mount_tag=hostusr,security_model=none,readonly=on',
                '-virtfs', f'local,path={work},mount_tag=probe,security_model=none,readonly=on']
    (work / 'command.json').write_text(json.dumps(command, indent=2)+'\n')
    with (work / 'guest.log').open('wb') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
    log = (work / 'guest.log').read_text(errors='replace')
    if 'TEGU_METAL_ROOT_READBACK_OK' not in log or 'TEGU_METAL_ROOT_OK' not in log or 'TEGU_METAL_ROOT_FAILED' in log:
        raise SystemExit('Guest install/readback failed; inspect guest.log')
    if args.offscreen_ui:
        import re
        match=re.search(r'TEGU_METAL_OFFSCREEN_MANIFEST_OK sha256=([0-9a-f]{64})',log)
        if not match: raise SystemExit('Offscreen UI manifest was not verified in readonly guest readback')
        result.update(offscreen_ui_diagnostic=True,
            offscreen_ui_manifest='/var/db/tegu-compositor-offscreen-ui.json',
            offscreen_ui_manifest_sha256=match.group(1),
            offscreen_disabled_launchd_jobs=['com.apple.backboardd','com.apple.SpringBoard'])
    elif 'TEGU_METAL_UI_JOBS_UNCHANGED_OK' not in log:
        raise SystemExit('Default root unexpectedly changed or failed to verify the two UI jobs')
    for flag, name in (('-fy','fsck-repair.log'),('-fn','fsck-verify.log')):
        with (work / name).open('wb') as log:
            check=subprocess.run([str(args.fsck.resolve()),flag,str(output)],stdout=log,stderr=subprocess.STDOUT)
        if flag=='-fn' and (check.returncode or 'appears to be OK' not in (work/name).read_text()):
            raise SystemExit('Probe image failed HFS verification')
    for key in list(result):
        if key.startswith('flash_') or key in ('deployment','boot_tested','device_readback_verified'):
            result.pop(key,None)
    result.update(image=str(output),sha256=digest(output),parent_image=str(source),deployed=False,
                  metal_probe_installed=True,metal_probe_device_tested=False)
    if args.null_tee_stdio:
        result['metal_probe_stdio_mode'] = 'null-tee-diagnostic'
    if args.bundle:
        result.update(metal_bundle_installed=True, metal_bundle_device_load_tested=False)
    (work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
