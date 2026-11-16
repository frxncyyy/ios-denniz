#!/usr/bin/env python3
"""Compare TGR5 state with the hardware-qualified compiled texture packet.

This checks consumed descriptors/FAUs and CSF register events against the
standalone packet, plus invalid-resource output preservation. No GPU submission.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys
from check_render_varying_state import Addresses, State, events, package
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gpu_compiled_texture_image import prepare


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('graphics', 'fetch', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    image, q = prepare(0x880100000, a.graphics)
    manifest = json.loads((a.graphics / 'result.json').read_text())
    fetch = json.loads(a.fetch.read_text())
    legacy = package(3, manifest, a.graphics, fetch)
    data = bytearray(len(legacy) + 128)
    data[:128], data[256:] = legacy[:128], legacy[128:]
    struct.pack_into('<I', data, 4, 5)
    struct.pack_into('<9I', data, 128, 1, 0, 0, 1, 0, 0, 1, 0, 15)
    struct.pack_into('<7I', data, 192, 1, 16, 0, 0, 0, 0, 1)
    binary = bytes(data)
    (a.out / 'pipeline-v5.tgr').write_bytes(binary)
    source = Path(__file__).resolve().parent
    library = a.out / 'render.so'
    subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-shared', '-fPIC', str(source / 'render_state.c'), '-o', str(library)], check=True)
    lib = C.CDLL(str(library.resolve()))
    entry = lib.tegu_render_build_resources
    entry.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Addresses), C.c_uint64,
                      C.c_uint64, C.POINTER(State)]
    entry.restype = C.c_int
    addresses = Addresses(q['descriptor_va'], q['tiler_context_va'], q['tiler_geometry_va'],
        q['tiler_chunk_va'], q['va'] + 0x3000, q['vertex_code_base'], q['shader_va'], 0xff3366cc)
    vertex = q['vertex_resource_table']
    fragment = q['texture_descriptors_va'] | 4
    state = State()
    assert not entry(binary, len(binary), C.byref(addresses), vertex, fragment, C.byref(state))
    actual = bytes(state.descriptors)
    base = q['descriptor_va'] - q['va'] + 0x4000
    reference = bytearray(image[base:base + 4096])
    # Output-flags word is outside this VS's consumed FAU footprint.
    assert manifest['vs']['fau_words'] == 7 and manifest['vs']['sysval_mask'] == 3
    assert struct.unpack_from('<I', reference, 0xa20)[0] == 0
    struct.pack_into('<I', reference, 0xa20, 1)
    for start, end in ((0, 0x200), (0x400, 0x620), (0x700, 0x710), (0xa00, 0xc00)):
        assert actual[start:end] == reference[start:end], hex(start)
    assert bytes(state.tiler) == image[0x21000:0x21100]
    commands = list(state.commands)[:state.command_count]
    reference_commands = struct.unpack_from('<%dQ' % (q['command_bytes'] // 8), image, 0x4000)
    assert len(commands) == 58 and events(commands) == events(reference_commands)
    # Invalid/missing/overlapping fragment resources, or texture state supplied
    # to an older pipeline, must fail before changing any caller output byte.
    refused = 0
    for packet, vs, fs in [(binary, vertex, n) for n in
        (0, 1, 4, fragment ^ 1, (1 << 48) | 4, vertex,
         addresses.descriptors | 4, addresses.vertex_code | 4)] + [(legacy, vertex, fragment)]:
        C.memset(C.byref(state), 0xa5, C.sizeof(state))
        prior = bytes(state)
        assert entry(packet, len(packet), C.byref(addresses), vs, fs, C.byref(state))
        assert bytes(state) == prior
        refused += 1
    # Every texture metadata/padding byte remains validated by the parser.
    for at in range(192, 256):
        bad = bytearray(binary); bad[at] ^= 0x80
        assert entry(bytes(bad), len(bad), C.byref(addresses), vertex, fragment, C.byref(state))
        assert bytes(state) == prior
        refused += 1
    (a.out / 'result.json').write_text(json.dumps(dict(passed=True, gpu_executed=False,
        descriptor_oracle=True, command_register_oracle=True, refusals=refused), indent=2) + '\n')
    print(f'PASS TGR5 texture descriptor/FAU/CSF packet parity, {refused} preserved-output refusals; GPU_EXECUTED=0')


if __name__ == '__main__':
    main()
