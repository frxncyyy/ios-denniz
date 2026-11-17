#!/usr/bin/env python3
"""Exercise the Nord2 audio-ID core and its actual native binding on the host.

Fixtures simulate revision3 snapshot MMIO and the I2Cv2 hardware controller
with TFA98xx and shared-bus bystanders; no physical MMIO, GPIO or I2C is touched.
Optional: CROSS=1 also compiles both helpers freestanding for arm64 (needs
clang) and checks that the NORD2 guard produces empty helper objects without it.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HELPERS = ROOT / 'shim/nord2-xnu'
FIXTURES = ROOT / 'tools/fixtures'


def run(command):
    subprocess.run(command, check=True)


def main():
    cc = os.environ.get('CC', 'cc')
    with tempfile.TemporaryDirectory(prefix='nord2-audio-id-') as directory:
        for case in ('core', 'bind'):
            binary = Path(directory) / case
            command = [cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                       '-O1', '-g', '-DNORD2=1', '-fsanitize=address,undefined',
                       '-fno-sanitize-recover=all',
                       '-I' + str(HELPERS), '-I' + str(FIXTURES),
                       str(FIXTURES / ('nord2-audio-id-' + case + '.c')),
                       str(HELPERS / 'audio_id.c'), str(HELPERS / 'audio_i2c8.c'),
                       str(HELPERS / 'audio_afe.c'),
                       '-o', str(binary)]
            run(command)
            run([str(binary)])
        if os.environ.get('CROSS'):
            clang = os.environ.get('CLANG', 'clang')
            for name in ('audio_id', 'audio_i2c8', 'audio_afe'):
                obj = Path(directory) / (name + '.o')
                run([clang, '-target', 'arm64-apple-macos14.0', '-ffreestanding',
                     '-fno-builtin', '-mcpu=generic', '-march=armv8.2-a+crypto',
                     '-DNORD2=1', '-O2', '-Wall', '-Wextra', '-Werror', '-c',
                     str(HELPERS / (name + '.c')), '-o', str(obj)])
            # Without NORD2 the helper TU must be empty (alternate board build).
            nm = shutil.which('llvm-nm') or shutil.which('llvm-nm-23')
            strings = shutil.which('strings')
            for name in ('audio_id', 'audio_i2c8', 'audio_afe'):
                obj = Path(directory) / (name + '.non-nord2.o')
                run([clang, '-target', 'arm64-apple-macos14.0', '-ffreestanding',
                     '-fno-builtin', '-mcpu=generic', '-march=armv8.2-a+crypto',
                     '-O2', '-Wall', '-Wextra', '-Werror', '-c',
                     str(HELPERS / (name + '.c')), '-o', str(obj)])
                objyes = Path(directory) / (name + '.yes-nord2.o')
                run([clang, '-target', 'arm64-apple-macos14.0', '-ffreestanding',
                     '-fno-builtin', '-mcpu=generic', '-march=armv8.2-a+crypto',
                     '-DNORD2=1', '-O2', '-Wall', '-Wextra', '-Werror', '-c',
                     str(HELPERS / (name + '.c')), '-o', str(objyes)])
                if nm:
                    text = subprocess.run([nm, str(obj)], check=True,
                                          capture_output=True, text=True).stdout
                    yes = subprocess.run([nm, str(objyes)], check=True,
                                         capture_output=True, text=True).stdout
                    assert 'nau_' not in text and 'na8_' not in text and 'naf_' not in text, text
                    assert 'nau_' in yes or 'na8_' in yes or 'naf_' in yes, yes
                elif strings:
                    text = subprocess.run([strings, str(obj)], check=True,
                                          capture_output=True, text=True).stdout
                    yes = subprocess.run([strings, str(objyes)], check=True,
                                         capture_output=True, text=True).stdout
                    assert 'nau_' not in text and 'na8_' not in text and 'naf_' not in text, text
                    assert 'nau_' in yes or 'na8_' in yes or 'naf_' in yes, yes
    print('PASS nord2 audio-id core and native binding')


if __name__ == '__main__':
    main()
