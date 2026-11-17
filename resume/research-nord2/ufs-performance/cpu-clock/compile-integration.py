#!/usr/bin/env python3
"""Compile private full UFS candidate with captured CI flags/local active headers."""
from pathlib import Path
import shlex,subprocess,json
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3]
build=ROOT/'research/ci-39158169212/buildtree';xnu=build/'xnu'
line=next(s for s in (ROOT/'research/ci-39503015652/build.log').read_text().splitlines() if 'clang -c ' in s and 'sweet_ufs.c' in s)
a=shlex.split(line[line.index('/Applications/'):]);out=['clang','--target=arm64-apple-macos14.0']
i=1
while i<len(a):
 arg=a[i];i+=1
 if arg in ('-MD','-MP'):continue
 if arg in ('-MF','-arch'):
  i+=1;continue
 if arg.endswith('/sweet_ufs.c'):continue
 if arg.startswith('-I/Applications/'):arg='-I'+str(build/'sdk-kernel-platform')
 arg=arg.replace('/Users/runner/work/ios-sweet/ios-sweet/dxb',str(build))
 out.append(arg)
out+=['-I'+str(xnu/'bsd/dev'),'-include',str(xnu/'osfmk/arm64/sweet_early.h'),'-o',str(HERE/'sweet_ufs.o'),str(HERE/'sweet_ufs.c')]
(HERE/'compile-command.json').write_text(json.dumps(out,indent=2)+'\n')
result=subprocess.run(out,cwd=build/'build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT',capture_output=True,text=True)
(HERE/'compile.log').write_text(result.stdout+result.stderr)
print('PASS private fullUFS AArch64integrationcompile' if result.returncode==0 else result.stderr[-8000:])
raise SystemExit(result.returncode)
