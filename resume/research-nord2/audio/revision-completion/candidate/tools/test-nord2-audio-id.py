#!/usr/bin/env python3
"""Exercise the Nord2 audio-ID core and its actual native binding on the host.

Fixtures simulate revision2 snapshot MMIO and the I2Cv2 hardware controller
with TFA98xx and shared-bus bystanders; no physical MMIO, GPIO or I2C is touched.
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
                       '-O1', '-g', '-DNORD2=1', '-fsanitize=address,undefined',
                       '-fno-sanitize-recover=all',
                       '-I' + str(HELPERS), '-I' + str(FIXTURES),
                       str(FIXTURES / ('nord2-audio-id-' + case + '.c')),
                       str(HELPERS / 'audio_id.c'), str(HELPERS / 'audio_i2c8.c'), '-o', str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
        if os.environ.get('CROSS'):
            for name in ('audio_id','audio_i2c8'):
                obj = Path(directory) / (name+'.o')
                subprocess.run([os.environ.get('CLANG','clang'),'-target','arm64-apple-macos14.0','-ffreestanding','-fno-builtin','-mcpu=generic','-march=armv8.2-a+crypto','-DNORD2=1','-O2','-Wall','-Wextra','-Werror','-c',str(HELPERS/(name+'.c')),'-o',str(obj)],check=True)
    print('PASS nord2 audio-id core and native binding')


if __name__ == '__main__':
    main()
