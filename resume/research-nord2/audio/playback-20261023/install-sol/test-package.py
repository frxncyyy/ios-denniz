#!/usr/bin/env python3
import importlib.util,pathlib,struct,tempfile,subprocess
HERE=pathlib.Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('package',HERE/'package.py'); m=importlib.util.module_from_spec(s); s.loader.exec_module(m)
plist=(HERE.parent/'upstream/topaz/userland/audio/audio_plugin.plist').read_bytes()
binary=struct.pack('<IIIIIIII',0xfeedfacf,0x100000c,0,8,0,0,0,0)+b'parser fixture, not executable'
blob=m.encode(binary,plist)
assert m.decode(blob)==(binary,plist)
for i in range(len(blob)):
    broken=bytearray(blob); broken[i]^=1
    try: m.decode(broken)
    except ValueError: pass
    else: raise AssertionError(i)
with tempfile.TemporaryDirectory() as tmp:
    tmp=pathlib.Path(tmp); (tmp/'package.bin').write_bytes(blob)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(HERE/'test-package.c'),'-lcrypto','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test'),str(tmp/'package.bin')],check=True)
print('PASS Python package mutation tests; fixture is synthetic, no plugin built')
