#!/usr/bin/env python3
"""Check sampled texture packing against pinned Mesa and qualified GPU packets.

Host-only goldens and ASan/UBSan refusal tests; does not submit a GPU draw.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys


def main():
    source = Path(__file__).resolve().parent
    repo = source.parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--mesa-source', type=Path, default=repo/'research/tegu-gpu/mesa-compiler-src')
    p.add_argument('--mesa-build', type=Path, default=repo/'research/tegu-gpu/mesa-compiler-build')
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=False)
    header = a.mesa_build/'src/panfrost/genxml/v10_pack.h'
    assert hashlib.sha256(header.read_bytes()).hexdigest() == \
        'b73468a372af9bfcaaa3e9f29cfdf36d3e79fbdb839dc27a96d4fa160160e2d1'
    includes = ['-DUTIL_ARCH_LITTLE_ENDIAN=1', '-DUTIL_ARCH_BIG_ENDIAN=0', '-DHAVE_ENDIAN_H=1'] + \
        ['-I'+str(v) for v in (a.mesa_source/'include', a.mesa_source/'src',
         a.mesa_source/'src/panfrost', a.mesa_build/'src', a.mesa_build/'src/panfrost')]
    warnings = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wsign-conversion']
    obj = a.work/'texture-freestanding.o'
    subprocess.run(['clang', *warnings, '-ffreestanding', '-nostdlib', '-O2', '-c',
                    str(source/'texture_state.c'), '-o', str(obj)], check=True)
    assert not subprocess.check_output(['nm', '-u', str(obj)]).strip()
    for mode, flags in [('release', ['-O2']), ('asan', ['-O1', '-g',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
        objects = []
        for name, extra in [('texture_state', warnings),
                ('texture_state_mesa_golden', ['-std=gnu11', *includes]),
                ('texture_state_host_test', ['-std=c11', '-Wall', '-Wextra', '-Werror'])]:
            output = a.work/(name+'-'+mode+'.o')
            subprocess.run(['clang', *extra, *flags, '-c', str(source/(name+'.c')),
                            '-o', str(output)], check=True)
            objects.append(str(output))
        binary = (a.work/('test-'+mode)).resolve()
        subprocess.run(['clang', *flags, *objects, '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(a.work/('page-'+mode+'.bin'))], check=True)
    page = (a.work/'page-release.bin').read_bytes()
    assert page == (a.work/'page-asan.bin').read_bytes()
    # Relocate the actual handwritten texture-r1 descriptors, rather than
    # trusting a second copy of the new packer's constants.
    sys.path.insert(0, str(source.parent))
    from gpu_texture_image import populate
    old, queue = bytearray(0x21000), dict(va=0x40000000)
    populate(old, queue, 16, 16, [0]*256)
    expected = bytearray(4096)
    expected[0x100:0x160] = old[0x20800:0x20860]
    struct.pack_into('<QQ', expected, 0, 0x10000100 | (1 << 56), 64)
    struct.pack_into('<Q', expected, 0x130, 0x10000140)
    struct.pack_into('<Q', expected, 0x148, 0x10001000)
    struct.pack_into('<I', expected, 0x100, struct.unpack_from('<I', expected, 0x100)[0] | (1 << 25))
    assert page == expected
    print('PASS Mesa descriptor goldens, qualified packet parity, freestanding/ASan/UBSan; GPU_EXECUTED=0')


if __name__ == '__main__':
    main()
