import subprocess
import tempfile
import unittest
from pathlib import Path

HERE=Path(__file__).resolve().parent

class PublicIOSurfaceProbeTests(unittest.TestCase):
    def test_strided_offset_mapping_and_canaries(self):
        source=r'''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "compositor_public_iosurface_layout.h"
int main(void) {
 struct tegu_public_surface_layout l={16,16,128,0,16384};
 unsigned char *backing=malloc(l.allocation_bytes), expected[1024];
 uint32_t pixels=99,guards=99;assert(backing);
 memset(backing,0xa5,l.allocation_bytes);
 for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(unsigned char)(i*17u+3u);
 for(unsigned y=0;y<16;y++)memcpy(backing+l.offset+y*l.row_bytes,expected+y*64,64);
 assert(tegu_public_surface_layout_valid(&l));
 assert(tegu_public_surface_verify(backing,&l,expected,0xa5,&pixels,&guards));
 assert(pixels==0&&guards==0);
 backing[l.offset+7*l.row_bytes+63]^=1;
 assert(tegu_public_surface_verify(backing,&l,expected,0xa5,&pixels,&guards));assert(pixels==1&&guards==0);
 backing[l.offset+7*l.row_bytes+63]^=1;backing[l.offset+8*l.row_bytes+64]^=1;
 assert(tegu_public_surface_verify(backing,&l,expected,0xa5,&pixels,&guards));assert(pixels==0&&guards==1);
 backing[l.offset+8*l.row_bytes+64]^=1;backing[2048]^=1;
 assert(tegu_public_surface_verify(backing,&l,expected,0xa5,&pixels,&guards));assert(guards==1);
 l.offset=4096;assert(!tegu_public_surface_layout_valid(&l));l.offset=0;l.row_bytes=64;assert(!tegu_public_surface_layout_valid(&l));
 l.row_bytes=128;backing[2048]^=1;backing[l.allocation_bytes-1]^=1;
 assert(tegu_public_surface_verify(backing,&l,expected,0xa5,&pixels,&guards));assert(pixels==0&&guards==1);
 free(backing);
 l.allocation_bytes=4096;assert(!tegu_public_surface_layout_valid(&l));
 l.allocation_bytes=32768;assert(!tegu_public_surface_layout_valid(&l));return 0;
}'''
        with tempfile.TemporaryDirectory() as td:
            c=Path(td)/'test.c';exe=Path(td)/'test';c.write_text(source)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer','-I',str(HERE),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_probe_uses_real_selector17_surface_and_lock_readback(self):
        source=(HERE/'compositor_public_probe.c').read_text()
        for token in ('IOSurfaceCreate','IOSurfaceGetID','IOSurfaceGetBytesPerRow','IOSurfaceGetAllocSize',
                      'IOSurfaceGetBaseAddress','IOSurfaceLock','IOSurfaceUnlock','IOSurfaceOffset',
                      'IOSurfaceTeguGPUShared','newTextureWithDescriptor:iosurface:plane:',
                      'tegu_public_surface_verify','TEGU_PUBLIC_SURFACE_ROW_BYTES,TEGU_PUBLIC_SURFACE_OFFSET'):
            self.assertIn(token,source)
        self.assertIn('surface_api.lock(target_surface,0,&seed)',source)
        self.assertIn('target_surface&&surface_api.release',source)
        self.assertIn('safe_release(target)',source)

    def test_builder_mode_is_opt_in_and_separate_from_load(self):
        builder=(HERE/'build_compositor_public_probe.py').read_text()
        self.assertIn("p.add_argument('--iosurface-target'",builder)
        self.assertIn("flags.append('-DTEGU_COMPOSITOR_IOSURFACE_PROBE')",builder)
        self.assertIn("launch += ['--iosurface-target']",builder)
        self.assertIn('--iosurface-target and --load-two-draws are separate qualification modes',builder)

if __name__=='__main__':unittest.main()
