#!/usr/bin/env python3
"""Compare TGR4 native blend state with GPU-qualified standalone packets."""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from gpu_blend_image import prepare, MODES
from check_render_varying_state import Addresses, State, events, package


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('graphics','fetch','out'):parser.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    manifest=json.loads((args.graphics/'result.json').read_text())
    fetch=json.loads(args.fetch.read_text())
    legacy=package(3,manifest,args.graphics,fetch)
    source=Path(__file__).resolve().parent;so=args.out/'state.so'
    subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
                    str(source/'render_state.c'),'-o',str(so)],check=True)
    lib=C.CDLL(str(so.resolve()));build=lib.tegu_render_build_vertex
    build.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Addresses),C.c_uint64,C.POINTER(State)];build.restype=C.c_int
    inputs={'source-alpha':(0,4,5,0,4,5),'premultiplied':(0,1,5,0,1,5),'separate':(0,4,5,0,1,5)}
    for mode in MODES:
        image,q=prepare(0x880100000,args.graphics,mode)
        data=bytearray(legacy[:128]+bytes(64)+legacy[128:]);struct.pack_into('<I',data,4,4)
        struct.pack_into('<9I',data,128,1,1,*inputs[mode],15)
        path=args.out/(mode+'.tgr');path.write_bytes(data)
        state=State();a=Addresses(q['descriptor_va'],q['tiler_context_va'],q['tiler_geometry_va'],
            q['tiler_chunk_va'],q['va']+0x3000,q['vertex_code_base'],q['shader_va'],q['token'])
        assert not build(bytes(data),len(data),C.byref(a),q['vertex_resource_table'],C.byref(state))
        ref=bytearray(image[0x20000:0x21000]);struct.pack_into('<I',ref,0xa20,1)
        for start,end in ((0,0x200),(0x400,0x620),(0x700,0x710),(0xa00,0xc00)):
            assert bytes(state.descriptors)[start:end]==ref[start:end],(mode,hex(start))
        assert bytes(state.tiler)==image[0x21000:0x21100]
        commands=struct.unpack_from('<%dQ'%(q['command_bytes']//8),image,0x4000)
        assert state.command_count==58 and events(list(state.commands)[:58])==events(commands)
        # No pair/no vertex fetch must still be usable for vertex-ID shaders.
        nofetch=bytearray(data);struct.pack_into('<8I',nofetch,96,*([0]*8))
        nofetch[448:1344]=bytes(896)
        assert not build(bytes(nofetch),len(nofetch),C.byref(a),0,C.byref(state))
        assert state.command_count==57
    sanitized=args.out/'sanitized'
    subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-I'+str(source),str(source/'render_state.c'),
        str(source/'render_blend_state_host_test.c'),'-o',str(sanitized)],check=True)
    subprocess.run([str(sanitized.resolve()),str(args.out/'source-alpha.tgr')],check=True)
    (args.out/'result.json').write_text(json.dumps(dict(passed=True,gpu_executed=False,
        modes=list(MODES),packet_oracle=True,sanitized=True,no_vertex_fetch_supported=True),indent=2)+'\n')
    print('PASS TGR4 descriptor/FAU/draw flags match all three GPU-qualified blend images; GPU_EXECUTED=0')


if __name__=='__main__':main()
