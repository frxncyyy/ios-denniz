#!/usr/bin/env python3
"""Build real local support libraries for the preserved private kernel link.
All downloads stay ignored; source pins and hashes are recorded. No device.
"""
from pathlib import Path
import subprocess,urllib.request,json,hashlib
r=Path(__file__).resolve().parents[2];p=r/'gpu/build/kernel-native';p.mkdir(exist_ok=True)
ld=p/'libdispatch';pin='cdb61e93e803d0093635f356753a9a30c09836db'
if not ld.exists():
 subprocess.run(['git','clone','--depth','1','--branch','libdispatch-1415.140.2','https://github.com/apple-oss-distributions/libdispatch.git',str(ld)],check=True)
assert subprocess.check_output(['git','-C',str(ld),'rev-parse','HEAD'],text=True).strip()==pin
extra=['-I'+str(ld),'-I'+str(r/'gpu/build/fakeroot/usr/local/include/firehose'),'-DDISPATCH_USE_DTRACE=0','-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1','-DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0']
with (p/'firehose-build.log').open('w') as log:subprocess.run(['python3',str(r/'gpu/tools/xnu_staged_compile.py'),str(ld/'src/firehose/firehose_buffer.c'),'--out',str(p/'firehose_buffer.o')]+['--extra='+x for x in extra],stdout=log,stderr=log,check=True)
cr=p/'compiler-rt';cr.mkdir(exist_ok=True);(cr/'profile').mkdir(exist_ok=True)
names=['InstrProfiling','InstrProfilingBuffer','InstrProfilingWriter','InstrProfilingPlatformDarwin','InstrProfilingVersionVar']
files=[('lib/profile/'+n+'.c',n+'.c') for n in names]+[('lib/profile/'+n,n) for n in ['InstrProfiling.h','InstrProfilingInternal.h','InstrProfilingPort.h','InstrProfilingUtil.h']]+[('include/profile/InstrProfData.inc','profile/InstrProfData.inc')]
base='https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-15.0.7/compiler-rt/';hashes={}
for remote,local in files:
 f=cr/local
 if not f.exists():f.write_bytes(urllib.request.urlopen(base+remote).read())
 hashes[remote]=hashlib.sha256(f.read_bytes()).hexdigest()
for n in names:
 with (cr/(n+'.log')).open('w') as log:
  subprocess.run(['clang','--target=arm64-apple-macos14.0','-isysroot',str(r/'gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk'),'-ffreestanding','-mkernel','-O2','-fno-stack-protector','-I'+str(cr),'-c',str(cr/(n+'.c')),'-o',str(cr/(n+'.o'))],stdout=log,stderr=log,check=True)
subprocess.run(['clang','--target=arm64-apple-macos14.0','-c',str(r/'gpu/src/nord2_chkstk.S'),'-o',str(p/'nord2_chkstk.o')],check=True)
(p/'support-provenance.json').write_text(json.dumps({'libdispatch':pin,'compiler_rt_tag':'llvmorg-15.0.7','source_sha256':hashes,'stack_probe':'authored; ABI checked against preserved CI40034353275 kernel','no_stubs':True},indent=2)+'\n')
print('PASS native firehose/profiling/stack-probe support; no device execution')
