#!/usr/bin/env python3
from pathlib import Path
import shlex,subprocess,json
ROOT=Path(__file__).resolve().parents[3];P=Path(__file__).resolve().parent;B=ROOT/'research/ci-39398655023/buildtree'
line=next(l for l in (ROOT/'research/ci-39398655023/build.log').read_text().splitlines() if 'clang++ -o SweetBattery.cpo' in l)
raw=shlex.split(line[line.index('/Applications/'):]);args=['clang++','-D__builtin_xnu_type_summary(x)=0','-D__builtin_xnu_type_signature(x)=""','-D__builtin_xnu_types_compatible(x,y)=1','-target','arm64-apple-macos14','-DNORD2=1','-I'+str(P)];i=1
while i<len(raw):
 a=raw[i];i+=1
 if a in ('-o','-MF','-arch'):i+=1;continue
 if a in ('-MD','-MP') or a.startswith(('-mmacosx-version-min=','-Wxnu-')):continue
 a=a.replace('/Users/runner/work/ios-sweet/ios-sweet/dxb',str(B))
 if a.startswith('-I/Applications/'):a='-I'+str(B/'sdk-kernel-platform')
 if a.endswith('/iokit/Kernel/arm/SweetBattery.cpp'):a=str(P/'SweetBattery.candidate.cpp')
 if a=='-I.':a='-I'+str(B/'build/xnu.obj/DEVELOPMENT_ARM64_SWEET/iokit/DEVELOPMENT')
 args.append(a)
args+=['-o',str(P/'SweetBattery.candidate.o')]
(P/'compile-command.json').write_text(json.dumps(args,indent=2)+'\n')
with (P/'compile.log').open('w') as out:subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,check=True)
for f in ['nord2_battery.c','mtk_i2c_read.c']:
 subprocess.run(['clang','-target','arm64-apple-macos14','-ffreestanding','-Wall','-Wextra','-Werror','-O2','-c',str(P/f),'-o',str(P/(f+'.o'))],check=True)
platform=[a for a in args if not a.endswith('SweetBattery.candidate.cpp')]
platform[platform.index('-o')+1]=str(P/'nord2_battery_platform.o')
platform.insert(platform.index('-o'),str(P/'nord2_battery_platform.cpp'))
with (P/'platform-compile.log').open('w') as out:subprocess.run(platform,stdout=out,stderr=subprocess.STDOUT,check=True)
print('PASS: private ARM64 SweetBattery + nativeI2C platform/PIO + collector objects (Apple typed-allocator compatibility macros for C++ only)')
