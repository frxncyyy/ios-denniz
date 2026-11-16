#!/usr/bin/env python3
"""Bounded host integration check; does not claim GPU execution/equivalence."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import subprocess
import struct
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True, type=Path)
    p.add_argument('--library', type=Path, help='Also exercise the in-process C ABI')
    p.add_argument('spirv', type=Path)
    args = p.parse_args()
    compiler, source = args.compiler.resolve(), args.spirv.resolve()
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        binaries = []
        packages = []
        for index in range(2):
            output = work/f'output-{index}.bin'
            package = work/f'output-{index}.tgp'
            result = subprocess.run([str(compiler), str(source), str(output), str(package)],
                                    capture_output=True, timeout=20)
            if result.returncode:
                raise RuntimeError(result.stderr.decode(errors='replace'))
            binaries.append(output.read_bytes())
            packed = package.read_bytes()
            words = struct.unpack('<16I', packed[:64])
            assert words[0:2] == (0x31504754, 1) and words[3] == 0xb8a21030
            assert words[2] == len(binaries[-1]) and packed[64:] == binaries[-1]
            packages.append(packed)
            print(result.stdout.decode().strip())
        assert binaries[0] and binaries[0] == binaries[1], 'non-deterministic machine code'
        assert packages[0] == packages[1], 'non-deterministic pipeline package'
        assembly = subprocess.check_output(['spirv-dis', str(source)], text=True, timeout=10)
        # Exercise structural descriptor validation, independent of shader name.
        changed, count = re.subn(r'(OpDecorate\s+%\S+\s+Binding)\s+0\b', r'\1 8', assembly, count=1)
        if count != 1:
            raise ValueError('integration fixture must have binding0')
        (work/'invalid.spvasm').write_text(changed)
        subprocess.run(['spirv-as', '--target-env', 'vulkan1.2', str(work/'invalid.spvasm'),
                        '-o', str(work/'invalid.spv')], check=True, timeout=10)
        output = work/'refused.bin'
        result = subprocess.run([str(compiler), str(work/'invalid.spv'), str(output)],
                                capture_output=True, timeout=20)
        assert result.returncode == 4 and b'REFUSED' in result.stderr and not output.exists()
        if args.library:
            class Blob(C.Structure):
                _fields_ = [('data', C.c_void_p), ('size', C.c_size_t)]
            lib=C.CDLL(str(args.library.resolve()))
            compile=lib.tegu_mali_compile
            compile.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
            compile.restype=C.c_int
            free=lib.tegu_mali_free
            free.argtypes=[C.POINTER(Blob)];free.restype=None
            valid=source.read_bytes();invalid=(work/'invalid.spv').read_bytes()
            for data,expected in [(valid,0),(invalid,4),(valid,0),(b'x',2),(valid,0)]:
                # Exercise byte alignment independent of the source allocation.
                storage=C.create_string_buffer(b'x'+data)
                package=Blob();error=C.create_string_buffer(256)
                rc=compile(C.byref(storage,1),len(data),C.byref(package),error,len(error))
                assert rc==expected,(rc,error.value)
                if rc:
                    assert not package.data and not package.size and error.value
                else:
                    assert C.string_at(package.data,package.size)==packages[0]
                free(C.byref(package));free(C.byref(package))
                assert not package.data and not package.size
            print('PASS in-process compile, unaligned input, refusal/recovery, output disposal')
            class Spec(C.Structure):
                _fields_=[('id',C.c_uint32),('size',C.c_uint32),('data',C.c_uint8*8)]
            specialized=lib.tegu_mali_compile_specialized
            specialized.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Spec),C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
            specialized.restype=C.c_int
            constants=(Spec*4)()
            for local in (1,8,32,64):
                for i,value in enumerate((local,1,1,0)):
                    constants[i]=Spec(i,4,(C.c_uint8*8).from_buffer_copy(value.to_bytes(8,'little')))
                package=Blob();error=C.create_string_buffer(256)
                rc=specialized(valid,len(valid),constants,4,C.byref(package),error,len(error))
                assert rc==0,(rc,error.value)
                packet=C.string_at(package.data,package.size)
                words=struct.unpack('<16I',packet[:64])
                assert words[9:12]==(local,1,1),words
                if local==1:assert packet==packages[0]
                else:assert packet!=packages[0]
                free(C.byref(package))
            constants[1].id=0
            assert specialized(valid,len(valid),constants,4,C.byref(package),error,len(error))==2
            assert not package.data and error.value
            constants[1].id=1;constants[1].size=3
            assert specialized(valid,len(valid),constants,4,C.byref(package),error,len(error))==2
            assert not package.data and error.value
            assert specialized(valid,len(valid),None,1,C.byref(package),error,len(error))==2
            print('PASS specialization local sizes1/8/32/64, default equivalence, invalid table rejection')
        print(json.dumps(dict(machine_bytes=len(binaries[0]),
                              sha256=hashlib.sha256(binaries[0]).hexdigest(),
                              deterministic=True, package_roundtrip=True, invalid_binding_refused=True,
                              device_execution_verified=False)))


if __name__ == '__main__':
    main()
