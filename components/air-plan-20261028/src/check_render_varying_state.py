#!/usr/bin/env python3
"""Compare native paired draw state to the compiled varying GPU image oracle.

Uses the authored AIR float4 fixture prepared by prepare_varying_probe.py and
its linked AIR vertex-fetch JSON. Also runs sanitized package/address checks.
This checks CPU-generated state; it does not submit a GPU job.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gpu_compiled_varying_image import prepare


class Addresses(C.Structure):
    _fields_ = [(n, C.c_uint64) for n in (
        'descriptors', 'tiler', 'geometry', 'chunk', 'target', 'vertex_code', 'fragment_code')
    ] + [('clear_rgba', C.c_uint32)]


class State(C.Structure):
    _fields_ = [('descriptors', C.c_ubyte * 4096), ('tiler', C.c_ubyte * 256),
                ('commands', C.c_uint64 * 128), ('command_count', C.c_uint32)]


def events(commands):
    """Independent 32-bit CSF register model, sampled at each non-MOVE command."""
    registers, result = {}, []
    for command in commands:
        op, reg = command >> 56, (command >> 48) & 255
        if op == 1:
            registers[reg] = command & 0xffffffff
            registers[(reg + 1) & 255] = (command >> 32) & 0xffff
        elif op == 2:
            registers[reg] = command & 0xffffffff
        else:
            result.append((command, dict(registers)))
    return result


def package(version, manifest, directory, fetch):
    code_offset = 1280
    vs, fs = [(directory / (name + '.mali')).read_bytes() for name in ('vs', 'fs')]
    fs_offset = code_offset + ((len(vs) + 63) & ~63)
    push, pair = manifest['push'], manifest['pair']
    data = bytearray(fs_offset + len(fs))
    words = [0x31524754, version, 0xb8a21030, 70, len(vs), len(fs),
             push['vertex_span'], push['fragment_span']] + [0] * 24
    for i, name in enumerate(('vs', 'fs')):
        row = manifest[name]
        preload = int(row['preload'], 16)
        words[8 + i * 8:16 + i * 8] = [row['registers'], row['fau_words'], row['sysval_mask'],
            row['tls'], preload & 0xffffffff, preload >> 32, row['push_bytes'], 0]
    words[24:26] = [fetch['mask'], fetch['count']]
    if version == 3:
        preload = int(pair['secondary_preload'], 16)
        words[26:32] = [pair['position_offset'], pair['secondary_offset'],
            pair['secondary_registers'], preload & 0xffffffff, preload >> 32, pair['generic_size_B']]
    struct.pack_into('<32I', data, 0, *words)
    for i, attribute in enumerate(fetch['attributes']):
        struct.pack_into('<7I', data, 384 + 28 * i, *[attribute[n] for n in (
            'location', 'buffer_index', 'format', 'offset', 'stride', 'input_rate', 'divisor')])
    for i, stage in enumerate(('vertex', 'fragment')):
        data[128 + 128 * i:256 + 128 * i] = bytes.fromhex(push[stage])
    data[code_offset:code_offset + len(vs)], data[fs_offset:] = vs, fs
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('graphics', 'fetch', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    image, q = prepare(0x880100000, args.graphics)
    manifest = json.loads((args.graphics / 'result.json').read_text())
    fetch = json.loads(args.fetch.read_text())
    assert fetch['version'] == 1 and fetch['count'] == len(fetch['attributes']) == 2
    assert fetch['mask'] == manifest['pair']['vs_attribute_mask'] == 3
    for i, attribute in enumerate(fetch['attributes']):
        assert attribute == dict(location=i, buffer_index=1, format=109,
                                 offset=16*i, stride=32, input_rate=0, divisor=1)
    packages = {v: package(v, manifest, args.graphics, fetch) for v in (2, 3)}
    for v, data in packages.items():
        (args.out / f'pipeline-v{v}.tgr').write_bytes(data)
    a = Addresses(q['descriptor_va'], q['tiler_context_va'], q['tiler_geometry_va'],
        q['tiler_chunk_va'], q['va'] + 0x3000, q['vertex_code_base'], q['shader_va'], 0xff3366cc)
    source = Path(__file__).resolve().parent
    library = args.out / 'render.so'
    subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    str(source / 'render_state.c'), '-o', str(library)], check=True)
    entry = C.CDLL(str(library.resolve())).tegu_render_build_vertex
    entry.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Addresses), C.c_uint64, C.POINTER(State)]
    entry.restype = C.c_int
    state = State()
    assert not entry(packages[3], len(packages[3]), C.byref(a), q['vertex_resource_table'], C.byref(state))
    descriptors = bytes(state.descriptors)
    base = q['descriptor_va'] - q['va'] + 0x4000
    reference = bytearray(image[base:base + 4096])
    # Legacy output-flags word lies outside the paired VS FAU window (7 words
    # rounded to 8). Both specialized variants omit this runtime sysval.
    assert manifest['vs']['fau_words'] == 7 and manifest['vs']['sysval_mask'] == 3
    assert struct.unpack_from('<I', reference, 0xa20)[0] == 0
    assert struct.unpack_from('<I', descriptors, 0xa20)[0] == 1
    struct.pack_into('<I', reference, 0xa20, 1)
    for start, end in ((0, 0x200), (0x400, 0x620), (0x700, 0x710), (0xa00, 0xc00)):
        assert descriptors[start:end] == reference[start:end], hex(start)
    assert bytes(state.tiler) == image[0x21000:0x21100]
    reference_commands = struct.unpack_from('<%dQ' % (q['command_bytes'] // 8), image, 0x4000)
    commands = list(state.commands)[:state.command_count]
    assert len(commands) == 58
    assert events(commands) == events(reference_commands)
    (args.out / 'descriptors.bin').write_bytes(descriptors)
    (args.out / 'commands.bin').write_bytes(struct.pack('<%dQ' % len(commands), *commands))
    sanitized = args.out / 'sanitized'
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-I' + str(source), str(source / 'render_state.c'),
        str(source / 'render_varying_state_host_test.c'), '-o', str(sanitized)], check=True)
    subprocess.run([str(sanitized.resolve()), str(args.out / 'pipeline-v3.tgr'),
        str(args.out / 'pipeline-v2.tgr'), hex(q['vertex_resource_table'])], check=True)
    (args.out / 'result.json').write_text(json.dumps(dict(passed=True, gpu_executed=False,
        descriptor_oracle=True, command_register_oracle=True, sanitized=True), indent=2) + '\n')
    print('PASS native paired draw state matches compiled varying GPU image; GPU_EXECUTED=0')


if __name__ == '__main__':
    main()
