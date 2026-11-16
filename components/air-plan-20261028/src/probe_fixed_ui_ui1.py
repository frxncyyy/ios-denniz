#!/usr/bin/env python3
"""Report which TFU1 writer predicate refuses each reconstructed fixed-UI variant.

Builds fixed_ui_ui1_probe.c against the supplied host bridge and Mali graphics
libraries, then, for each serial of check_fixed_ui_live_variants.py, runs the
R80 frontend plan, the R80 compile and every tegu_ui1_build predicate.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess
import tempfile

import check_fixed_ui_live_variants as live

HERE = Path(__file__).resolve().parent


def build_probe(bridge, compiler, directory):
    library = directory / 'probe.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-O1', '-I', str(HERE), str(HERE / 'fixed_ui_ui1_probe.c'),
                    str(bridge.resolve()), str(compiler.resolve()),
                    '-Wl,-rpath,' + str(bridge.resolve().parent), '-Wl,-rpath,' + str(compiler.resolve().parent),
                    '-o', str(library)], check=True)
    probe = C.CDLL(str(library))
    probe.tegu_fixed_ui_ui1_probe.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                                             C.c_char_p, C.c_size_t]
    return probe


def run(lib, base, profile, serial, probe):
    vertex = {'fc_generic': (53, b'\0'), 'fc_texcoord_count0': (49, b'\1'),
              'fc_texcoord_count1': (49, b'\1'), 'fc_vertex_layout': (49, b'\0')}
    fragment = live.baseline_fragment()
    for name, value in live.VARIANTS.get(serial, {}).items():  # serial 0: captured baseline
        ty, old = fragment[name]
        fragment[name] = (ty, value.to_bytes(len(old), 'little'))
    vs_lib = live.specialize(lib, base, 'fixed_vert_lph_spc', vertex)
    fs_lib = live.specialize(lib, base, 'fixed_frag_lph_cph', fragment)
    attributes = (live.VertexAttribute * 4)(
        live.VertexAttribute(0, 31, 0, 1), live.VertexAttribute(1, 29, 16, 1),
        live.VertexAttribute(2, 29, 24, 1), live.VertexAttribute(3, 27, 32, 1))
    layouts = (live.VertexLayout * 1)(live.VertexLayout(1, 48, 1, 1))
    output = (live.Blob(), live.Blob(), live.Blob())
    error = C.create_string_buffer(2048)
    profile_buffer = C.create_string_buffer(profile)
    pipeline = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
        vs_lib, b'fixed_vert_lph_spc', 18, fs_lib, b'fixed_frag_lph_cph', 18, 80,
        attributes, 4, layouts, 1, profile_buffer, len(profile),
        C.byref(output[0]), C.byref(output[1]), C.byref(output[2]), error, len(error))
    lib.tegu_air_library_close(vs_lib)
    lib.tegu_air_library_close(fs_lib)
    try:
        if not pipeline:
            return {'translate': error.value.decode(errors='replace')}
        report = C.create_string_buffer(8192)
        probe.tegu_fixed_ui_ui1_probe(pipeline, output[0].bytes, output[0].length,
                                      output[1].bytes, output[1].length, report, len(report))
        return json.loads(report.value.decode(errors='replace'))
    finally:
        if pipeline:
            lib.tegu_air_render_pipeline_close(pipeline)
        for value in output:
            lib.tegu_air_blob_free(C.byref(value))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--metallib', type=Path, required=True)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--serial', type=int, action='append')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        probe = build_probe(args.bridge, args.compiler, Path(directory))
        lib = live.configure_bridge(args.bridge)
        metallib = args.metallib.read_bytes()
        buffer = C.create_string_buffer(metallib)
        error = C.create_string_buffer(1024)
        base = lib.tegu_air_library_open(buffer, len(metallib), error, len(error))
        if not base:
            raise SystemExit('AIR library open failed: ' + error.value.decode(errors='replace'))
        profile = live.profile_bytes(args.profile)
        try:
            for serial in args.serial or sorted(live.VARIANTS):
                print(json.dumps({'serial': serial, **run(lib, base, profile, serial, probe)}))
        finally:
            lib.tegu_air_library_close(base)


if __name__ == '__main__':
    main()
