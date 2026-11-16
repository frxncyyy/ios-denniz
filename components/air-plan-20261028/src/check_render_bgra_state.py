#!/usr/bin/env python3
"""Host parity for format-aware legacy native render state; no GPU execution."""
import argparse
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
METAL = Path(__file__).resolve().parent
RESEARCH = ROOT / "research/tegu-gpu"

class Addresses(C.Structure):
    _fields_ = [(n, C.c_uint64) for n in
                ("descriptors", "tiler", "geometry", "chunk", "target", "vertex_code", "fragment_code")] + [
                    ("clear_rgba", C.c_uint32)]

class State(C.Structure):
    _fields_ = [("descriptors", C.c_ubyte * 4096), ("tiler", C.c_ubyte * 256),
                ("commands", C.c_uint64 * 128), ("command_count", C.c_uint32)]

def compile_lib(src, out):
    subprocess.run(["clang", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                    str(src / "render_state.c"), "-o", str(out)], check=True)
    lib = C.CDLL(str(out))
    fn = lib.tegu_render_build_vertex
    fn.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Addresses), C.c_uint64, C.POINTER(State)]
    fn.restype = C.c_int
    return lib, fn

def build(fn, pkg, table=0):
    raw = C.create_string_buffer(bytes(pkg))
    addr = Addresses(0x100000, 0x200000, 0x300000, 0x400000, 0x500000,
                     0x600000, 0x700000, 0xA1B2C3D4)
    if table:
        table = 0x800004
    state = State()
    rc = fn(raw, len(pkg), C.byref(addr), table, C.byref(state))
    return rc, bytes(state)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', default='4095038', help='Git revision of hardware-qualified RGBA state before BGRA integration')
    args = parser.parse_args()
    paths = subprocess.check_output(['rg', '--files', '--hidden', '--no-ignore', '-g', '*.tgr', str(RESEARCH)], text=True).splitlines()
    packages = [Path(p) for p in sorted(paths) if "pipeline-v5" not in p]
    # Include every actual legacy package fixture; header format is word3.
    packages = [p for p in packages if struct.unpack_from("<I", p.read_bytes(), 4)[0] in (1, 2, 3, 4)]
    assert packages, "no actual TGR1..4 packages found"
    with tempfile.TemporaryDirectory() as tmp:
        baseline_dir = Path(tmp) / 'baseline'
        baseline_dir.mkdir()
        for name in ('render_state.c', 'render_state.h', 'render_pipeline_format.h',
                     'blend_state.c', 'blend_state.h', 'vertex_fetch.h'):
            old = subprocess.check_output(['git', '-C', str(ROOT), 'show',
                args.baseline_ref + ':tools/tegu/metal/' + name])
            (baseline_dir / name).write_bytes(old)
        baseline_lib, baseline = compile_lib(baseline_dir, Path(tmp) / 'baseline.so')
        proto_lib, prototype = compile_lib(METAL, Path(tmp) / "render.so")
        exercised = 0
        blend_cases = 0
        for path in packages:
            original = bytearray(path.read_bytes())
            version = struct.unpack_from("<I", original, 4)[0]
            fmt = struct.unpack_from("<I", original, 12)[0]
            if fmt != 70:
                continue
            table = bool(struct.unpack_from("<I", original, 96)[0]) if version >= 2 else False
            # The parser requires a resource table for any vertex input metadata.
            rc_rgba, rgba_state = build(prototype, original, table)
            assert rc_rgba == 0, (path, rc_rgba)
            old_rc, old_state = build(baseline, original, table)
            assert old_rc == 0 and rgba_state == old_state, (path, 'legacy RGBA state changed')

            bgra = bytearray(original)
            struct.pack_into("<I", bgra, 12, 80)
            rc, bgra_state = build(prototype, bgra, table)
            assert rc == 0, (path, rc)
            diffs = [i for i, (a, b) in enumerate(zip(rgba_state, bgra_state)) if a != b]
            # Word1's 12-bit swizzle changes byte 134 only (0x88 -> 0x0a).
            assert diffs == [134], (path, diffs[:20])
            assert struct.unpack_from("<I", rgba_state, 132)[0] == 0x86880299
            assert struct.unpack_from("<I", bgra_state, 132)[0] == 0x860A0299
            assert rgba_state[176:192] == bgra_state[176:192]
            # Blend descriptor and FAU constant are target-independent logical state.
            assert rgba_state[0x440:0x450] == bgra_state[0x440:0x450]
            assert rgba_state[0xb30:0xb38] == bgra_state[0xb30:0xb38]
            if version == 4:
                blend_cases += 1
            exercised += 1

        # TGR5 now admits RGBA8 sampled sources into RGBA/BGRA targets.
        # Its larger-target and FP16 source qualification is separate.
        texture = bytearray((RESEARCH / "native-texture-state-r1/pipeline-v5.tgr").read_bytes())
        resources = proto_lib.tegu_render_build_resources
        resources.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Addresses),
                              C.c_uint64, C.c_uint64, C.POINTER(State)]
        resources.restype = C.c_int
        addr = Addresses(0x100000, 0x200000, 0x300000, 0x400000, 0x500000,
                         0x600000, 0x700000, 0xA1B2C3D4)
        texture_states = []
        for fmt in (70, 80, 81):
            struct.pack_into('<I', texture, 12, fmt)
            raw = C.create_string_buffer(bytes(texture))
            state = State()
            rc = resources(raw, len(texture), C.byref(addr), 0x800004,
                           0x900004, C.byref(state))
            if fmt == 81:
                assert rc != 0, 'unsupported sampled-target format accepted'
            else:
                assert rc == 0, ('TGR5 sampled-target state refused', fmt, rc)
                texture_states.append(bytes(state))
        assert [i for i, (a, b) in enumerate(zip(*texture_states)) if a != b] == [134]

        # The format-aware blend converter accepts both memory formats while
        # returning identical logical tile/blend words.
        blend_header = METAL / "blend_state.h"
        assert blend_header.exists()
        print(f"PASS packages={exercised} TGR4_blend={blend_cases} RGBA_byte_identical=1 BGRA_only_byte134=1 logical_clear_blend_constant=unchanged TGR5_BGRA_only_byte134=1 unsupported_format_refused=1")

if __name__ == "__main__":
    main()
