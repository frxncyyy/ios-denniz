#!/usr/bin/env python3
"""Host-only readback checks for the opt-in SWEET /dev/null stdio path."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import shutil
import tempfile
import unittest
from unittest import mock
import gzip
import subprocess

HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


installer = load('nulltee_installer', HERE / 'install_native_probe.py')
preparer = load('nulltee_preparer', HERE / 'prepare_probe_root_vm.py')


def fake_macho():
    return (0xFEEDFACF).to_bytes(4, 'little') + (0x0100000C).to_bytes(4, 'little') + \
        (0).to_bytes(4, 'little') + (2).to_bytes(4, 'little') + bytes(16)


class NullTeeStdioTests(unittest.TestCase):
    def make_root(self, td):
        root = Path(td) / 'target'
        config = root / 'System/Library/xpc/launchd.plist'
        config.parent.mkdir(parents=True)
        (root / 'private/var/mobile/Library/Preferences').mkdir(parents=True)
        (root / 'private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist').write_bytes(
            plistlib.dumps({'SetupFinishedAllSteps': True}))
        original = {'LaunchDaemons': {'example': {'Label': 'example', 'ProgramArguments': ['/bin/true']}}}
        config.write_bytes(plistlib.dumps(original, fmt=plistlib.FMT_BINARY))
        probe_root = Path(td) / 'probe'
        probe_root.mkdir()
        binary = probe_root / 'probe'
        binary.write_bytes(fake_macho())
        (probe_root / 'tegu-metal-probe').write_bytes(binary.read_bytes())
        (probe_root / 'probe-arguments.json').write_text('["--replay"]\n')
        return root, config, probe_root, binary

    def test_actual_installer_and_readonly_guest_verifier_default_and_optin(self):
        for use_null_tee in (False, True):
            with self.subTest(null_tee_stdio=use_null_tee), tempfile.TemporaryDirectory() as td:
                root, config, probe_root, binary = self.make_root(td)
                output = io.StringIO()
                with contextlib.redirect_stdout(output), mock.patch.object(installer.os, 'chown'):
                    installer.install(root, binary, probe_arguments=['--replay'],
                                      null_tee_stdio=use_null_tee)
                job = plistlib.loads(config.read_bytes())['LaunchDaemons'][installer.JOB]
                expected = '/dev/null' if use_null_tee else '/dev/console'
                self.assertEqual(job['StandardOutPath'], expected)
                self.assertEqual(job['StandardErrorPath'], expected)
                self.assertEqual(job['ProgramArguments'], [installer.PROGRAM, '--replay'])
                self.assertEqual(job['RunAtLoad'], True)
                self.assertNotIn('KeepAlive', job)
                self.assertIn('stdio=/dev/null SWEET tee requested' if use_null_tee else 'console output', output.getvalue())
                readback = preparer.guest_readback_python(
                    False, target_root=str(root), probe_root=str(probe_root),
                    require_root_owner=False, null_tee_stdio=use_null_tee)
                verified = io.StringIO()
                with contextlib.redirect_stdout(verified):
                    exec(compile(readback, '<guest-readback>', 'exec'), {})
                self.assertIn('TEGU_METAL_ROOT_READBACK_OK', verified.getvalue())
                self.assertEqual('TEGU_METAL_NULL_TEE_STDIO_READBACK_OK' in verified.getvalue(), use_null_tee)
                self.assertFalse((root / installer.OFFSCREEN_MANIFEST).exists())

    def test_offscreen_readback_keeps_scope_and_combines_with_null_tee(self):
        source = preparer.guest_readback_python(True, null_tee_stdio=True)
        self.assertIn("stdio_path='/dev/null' if NULL_TEE_STDIO else '/dev/console'", source)
        self.assertIn("'StandardOutPath':stdio_path,'StandardErrorPath':stdio_path", source)
        self.assertIn("assert set(manifest['disabled_jobs'])=={'com.apple.backboardd','com.apple.SpringBoard'}", source)
        self.assertIn("assert job['StandardOutPath']==stdio_path and job['StandardErrorPath']==stdio_path", source)

    def test_both_entrypoints_expose_explicit_optin(self):
        self.assertIn("parser.add_argument('--null-tee-stdio'", (HERE / 'install_native_probe.py').read_text())
        self.assertIn("parser.add_argument('--null-tee-stdio'", (HERE / 'prepare_probe_root_vm.py').read_text())
        self.assertIn("(' --null-tee-stdio' if args.null_tee_stdio else '')",
                      (HERE / 'prepare_probe_root_vm.py').read_text())

    def test_preparer_propagates_flag_and_records_only_optin_mode(self):
        for use_null_tee in (False, True):
            with self.subTest(null_tee_stdio=use_null_tee), tempfile.TemporaryDirectory() as td:
                td = Path(td)
                build = td / 'build'; build.mkdir()
                source = td / 'source.img'; source.write_bytes(b'test image')
                source_manifest = {'image': str(source), 'sha256': __import__('hashlib').sha256(
                    source.read_bytes()).hexdigest(), 'fsck_verified': True,
                    'setup_skip_verified': True, 'full_bringup_verified': True}
                (build / 'source.json').write_text(json.dumps(source_manifest))
                (build / 'initramfs.cpio.gz').write_bytes(gzip.compress(make_newc_init(
                    b'for source in os systemos appos; do :; done\n')))
                (build / 'command.json').write_text(json.dumps(['qemu', '-initrd', 'old', '-drive', 'old']))
                probe = td / 'probe'; probe.write_bytes(fake_macho())
                work = td / 'work'; output = td / 'output.img'
                argv = ['prepare_probe_root_vm.py', '--build', str(build),
                        '--source-manifest', str(build / 'source.json'), '--work', str(work),
                        '--output', str(output), '--fsck', str(td / 'fsck'), '--probe', str(probe)]
                if use_null_tee:
                    argv.append('--null-tee-stdio')
                seen = {}

                def run(command, **kwargs):
                    if command[0] == 'cp':
                        shutil.copyfile(command[-2], command[-1])
                    elif command[0] == 'qemu':
                        archive = gzip.decompress((work / 'initramfs.cpio.gz').read_bytes())
                        init = next(data for _, name, data in preparer.entries(archive) if name == b'init\0').decode()
                        command_line = next(line for line in init.splitlines() if '/probe/install.py' in line)
                        seen['command'] = command_line
                        seen['readback'] = init.split('__VERIFY_SOURCE__')[-1] if '__VERIFY_SOURCE__' in init else init
                        if use_null_tee:
                            self.assertIn(' --null-tee-stdio', command_line)
                            self.assertIn("NULL_TEE_STDIO=True", init)
                            self.assertIn("stdio_path='/dev/null' if NULL_TEE_STDIO", init)
                            log = 'TEGU_METAL_NULL_TEE_STDIO_READBACK_OK\nTEGU_METAL_ROOT_READBACK_OK\nTEGU_METAL_ROOT_OK\nTEGU_METAL_UI_JOBS_UNCHANGED_OK\n'
                        else:
                            self.assertNotIn(' --null-tee-stdio', command_line)
                            self.assertIn("NULL_TEE_STDIO=False", init)
                            self.assertNotIn('metal_probe_stdio_mode', init)
                            log = 'TEGU_METAL_ROOT_READBACK_OK\nTEGU_METAL_ROOT_OK\nTEGU_METAL_UI_JOBS_UNCHANGED_OK\n'
                        (work / 'guest.log').write_text(log)
                    elif Path(command[0]).name == 'fsck':
                        kwargs['stdout'].write(b'appears to be OK')
                    else:
                        raise AssertionError(command)
                    return subprocess.CompletedProcess(command, 0)

                with contextlib.redirect_stdout(io.StringIO()), mock.patch.object(preparer.subprocess, 'run', side_effect=run), \
                     mock.patch.object(__import__('sys').modules['sys'], 'argv', argv):
                    preparer.main()
                result = json.loads((work / 'result.json').read_text())
                self.assertEqual(result.get('metal_probe_stdio_mode') == 'null-tee-diagnostic', use_null_tee)
                self.assertEqual(result['metal_probe_device_tested'], False)
                self.assertEqual(result['deployed'], False)


def make_newc_init(data):
    name = b'init\0'
    fields = [1, 0o100755, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0]
    archive = bytearray(b'070701' + b''.join(f'{n:08x}'.encode() for n in fields) + name)
    archive.extend(bytes((-len(archive)) % 4)); archive.extend(data); archive.extend(bytes((-len(archive)) % 4))
    trailer = b'TRAILER!!!\0'; fields = [0] * 13; fields[11] = len(trailer)
    archive.extend(b'070701' + b''.join(f'{n:08x}'.encode() for n in fields) + trailer)
    archive.extend(bytes((-len(archive)) % 4)); archive.extend(bytes((-len(archive)) % 512))
    return bytes(archive)


if __name__ == '__main__':
    unittest.main(verbosity=2)
