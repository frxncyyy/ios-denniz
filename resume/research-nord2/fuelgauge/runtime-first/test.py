#!/usr/bin/env python3
"""Private source/mock tests only; no native execution."""
from pathlib import Path
import subprocess,tempfile,json
P=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'pio'
 subprocess.run(['cc','-std=c11','-DNORD2=1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(P/'mtk_i2c_read.c'),str(P/'test-i2c.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 native=(P/'nord2_battery_platform.cpp').read_text()
 report=native[native.index('static unsigned bus_logged'):native.index('static vm_offset_t address')]
 # Strip sample variables not used by report_bus.
 report=report.replace('static unsigned sample_logged,sample_last_valid;','').replace('static int sample_last_rc;','')
 harness='''#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdarg>
#include "mtk_i2c_read.h"
#include "nbg_telemetry.h"
static unsigned lines;
static void IOLog(const char *fmt,...){char s[512];va_list a;va_start(a,fmt);int n=vsnprintf(s,sizeof(s),fmt,a);va_end(a);assert(n>0&&n<255);lines++;}
'''+report+'''int main(){nbg_i2c_state s={};s.prior_irq=1;s.prior_ack=1;report_bus(7,0,&s);assert(lines==3);report_bus(7,0,&s);assert(lines==3);s.failure=8;report_bus(7,-1,&s);assert(lines==6);IOLog(NBG_PREFLIGHT_LOG,UINT_MAX,UINT_MAX,UINT_MAX);}
'''
 f=Path(d)/'report.cpp';f.write_text(harness)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(P),str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
# Compile actual candidate with CI board-config flags; no forced NORD2.
base=json.loads((P.parent/'compile-command.json').read_text())
for name in ['mtk_i2c_read.c','nord2_battery_platform.cpp']:
 args=[a.replace('-I'+str(P.parent),'-I'+str(P)).replace(str(P.parent/'SweetBattery.candidate.cpp'),str(P/name)) for a in base if not a.startswith('-DNORD2')]
 if name.endswith('.c'):
  args=[a for a in args if a not in ['-std=gnu++1z','-fsized-deallocation','-fapple-kext','-fno-c++-static-destructors']];args[0]='clang'
 args[args.index('-o')+1]=str(P/(name+'.o'))
 subprocess.run(args,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
print('PASS: 62-callback unchanged zero-status path; 68-callback inherited completion path; every callback failure halts/no replay; 1022 noncompletion statuses refused without ack/START; busy/DMA/FIFO/change/sticky refused; bounded deduplicated telemetry; CI-derived ARM64 C/C++ no forced NORD2. No hardware.')
