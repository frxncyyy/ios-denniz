#!/usr/bin/env python3
from pathlib import Path
import json,subprocess
P=Path(__file__).resolve().parent;ROOT=P.parents[2];C=ROOT/'shim/nord2-xnu';OUT=P/'promotion';OUT.mkdir(exist_ok=True)
original=json.loads((P/'compile-command.json').read_text())
args=[a.replace('-I'+str(P),'-I'+str(C)).replace(str(P/'SweetBattery.candidate.cpp'),str(C/'SweetBattery.cpp')) for a in original if not a.startswith("-DNORD2=")]
args[args.index('-o')+1]=str(OUT/'SweetBattery.o')
commands=[]
with (OUT/'compile.log').open('w') as out:
 subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,check=True)
 platform=[str(C/'nord2_battery_platform.cpp') if a==str(C/'SweetBattery.cpp') else a for a in args]
 platform[platform.index('-o')+1]=str(OUT/'nord2_battery_platform.o')
 commands.append(platform)
 subprocess.run(platform,stdout=out,stderr=subprocess.STDOUT,check=True)
 for name in ['nord2_battery.c','mtk_i2c_read.c']:
  cargs=[a for a in args if a not in ['-std=gnu++1z','-fsized-deallocation','-fapple-kext','-fno-c++-static-destructors']]
  cargs[0]='clang'
  cargs=[str(C/name) if a==str(C/'SweetBattery.cpp') else a for a in cargs]
  cargs[cargs.index('-o')+1]=str(OUT/(name+'.o'))
  commands.append(cargs)
  subprocess.run(cargs,stdout=out,stderr=subprocess.STDOUT,check=True)
nm=ROOT.parent/'toolchains/proton-clang/bin/llvm-nm'
expected=['_nord2_battery_platform_sample','_nbg_collect','_nbg_i2c_read']
results=[]
for cmd,symbol in zip(commands,expected):
 assert not any(a.startswith('-DNORD2') for a in cmd)
 obj=Path(cmd[cmd.index('-o')+1])
 symbols=subprocess.check_output([str(nm),'--defined-only','--extern-only',str(obj)],text=True)
 assert symbol in symbols,symbol
 alternate=[a.replace('-DARM64_BOARD_CONFIG_SWEET','-DARM64_BOARD_CONFIG_T8103') for a in cmd]
 alternate[alternate.index('-o')+1]=str(obj)+'.non-nord2'
 subprocess.run(alternate,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 empty=subprocess.check_output([str(nm),'--defined-only','--extern-only',str(obj)+'.non-nord2'],text=True)
 assert not empty.strip(),empty
 results.append({'source':cmd[-1] if False else str(obj),'expected_export':symbol,'defined_symbols':symbols.strip(),'T8103_empty':True})
(OUT/'normal-ci-guard-check.json').write_text(json.dumps({'forced_NORD2':False,'kernel':True,'selected_board':'SWEET','alternate_board':'T8103','results':results,'commands':commands},indent=2)+'\n')
print('PASS: CI-derived KERNEL/SWEET flags without -DNORD2 export all helpers; KERNEL/T8103 objects empty; C++ Apple typedallocator compatibility caveat unchanged')
