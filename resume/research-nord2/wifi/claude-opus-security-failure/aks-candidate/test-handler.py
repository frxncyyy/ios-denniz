#!/usr/bin/env python3
"""Compile actual old/new generated TU with narrow XNU API doubles; never run on device."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
P=Path(__file__).resolve().parent
R=next(x for x in P.parents if(x/'AGENTS.md').exists())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path);args=ap.parse_args()
out=args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='nord2-aks-handler-'))
out.mkdir(parents=True,exist_ok=True)
headers=out/'include'
for name in ['pexpert/arm64/board_config.h','IOKit/IOService.h','IOKit/IOUserClient.h','IOKit/IOKitKeys.h','IOKit/IOLib.h','IOKit/IOMemoryDescriptor.h','libkern/c++/OSString.h','sys/proc.h']:
    p=headers/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "test-xnu.h"\n')
records=[]
for name,source,defines in [
    ('new',P/'proposed/iokit/Kernel/arm/SweetKeyStore.cpp',['-DNORD2=1']),
    ('old',P/'SweetKeyStore.base.cpp',['-DNORD2=1','-DAKS_EXPECT_UNSUPPORTED=1']),
    ('not-nord2',P/'proposed/iokit/Kernel/arm/SweetKeyStore.cpp',['-DAKS_EXPECT_UNSUPPORTED=1'])]:
    command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
             '-Wall','-Wextra','-Werror','-DSWEET=1',*defines,'-I'+str(headers),'-I'+str(P/'fixtures'),
             '-DAKS_SOURCE="'+str(source)+'"',str(P/'fixtures/test-handler.cpp'),'-o',str(out/name)]
    build=subprocess.run(command,capture_output=True,text=True)
    (out/(name+'-build.log')).write_text(build.stdout+build.stderr)
    assert build.returncode==0,(name,build.stderr)
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    result=subprocess.run([str(out/name)],capture_output=True,text=True,env=env)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0,(name,result.stdout,result.stderr)
    records.append(dict(name=name,source_sha256=sha(source),command=command,exit=result.returncode,log_sha256=sha(out/(name+'.log')),output=result.stdout.strip()))
report=dict(verdict='PASS_ACTUAL_GENERATED_HANDLER_ASAN_UBSAN',tests=records,
            limitations=['Host doubles execute actual production externalMethod/helper code, not real Mach IPC or hardware.',
                         'Descriptor write/complete failures may modify caller bytes; never report success or commit size/class metadata.',
                         'No encryption/authentication/SEP/secure persistence or Wi-Fi keychain causality claimed.'])
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(output=str(out),report_sha256=sha(out/'report.json'),results=[x['output'] for x in records]),indent=2))
