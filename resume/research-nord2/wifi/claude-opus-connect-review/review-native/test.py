#!/usr/bin/env python3
import pathlib,subprocess
P=pathlib.Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
C=P/'candidate';B=P/'_build';B.mkdir(exist_ok=True)
cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections','-I',str(C),'-I',str(R/'shim/nord2-xnu')]
cmd += [str(C/n) for n in ['test-followup.c','wifi_station_transport.c','wifi_station_wire.c']]
cmd += [str(R/'shim/nord2-xnu'/n) for n in ['wifi_normal_transport.c','wifi_main_command.c','wifi_scan_bss.c']]
cmd += ['-Wl,--gc-sections','-o',str(B/'tests')]
subprocess.run(cmd,check=True);subprocess.run([str(B/'tests')],check=True)
