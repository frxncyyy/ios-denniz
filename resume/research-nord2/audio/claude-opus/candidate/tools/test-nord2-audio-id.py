#!/usr/bin/env python3
"""Exercise the Nord2 audio-ID core and its actual native binding on the host.

Fixtures simulate MMIO, GPIO pads and an open-drain I2C8 bus with TFA98xx
models and the other I2C8 devices; no physical MMIO, GPIO or I2C is touched.
Optional: CROSS=1 also compiles audio_id.c freestanding for arm64 (needs clang).
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HELPERS = ROOT / 'shim/nord2-xnu'
FIXTURES = ROOT / 'tools/fixtures'


def main():
    cc = os.environ.get('CC', 'cc')
    with tempfile.TemporaryDirectory(prefix='nord2-audio-id-') as directory:
        for case in ('core', 'bind'):
            binary = Path(directory) / case
            command = [cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                       '-O1', '-g', '-fsanitize=address,undefined',
                       '-fno-sanitize-recover=all',
                       '-I' + str(HELPERS), '-I' + str(FIXTURES),
                       str(FIXTURES / ('nord2-audio-id-' + case + '.c')),
                       str(HELPERS / 'audio_id.c'), '-o', str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
        if os.environ.get('CROSS'):
            obj = Path(directory) / 'audio_id.o'
            subprocess.run([os.environ.get('CLANG', 'clang'), '-target', 'arm64-apple-macos14.0',
                            '-ffreestanding', '-fno-builtin', '-mcpu=generic',
                            '-march=armv8.2-a+crypto', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-c', str(HELPERS / 'audio_id.c'), '-o', str(obj)], check=True)
    print('PASS nord2 audio-id core and native binding')


if __name__ == '__main__':
    main()
