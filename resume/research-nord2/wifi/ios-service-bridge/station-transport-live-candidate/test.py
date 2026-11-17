#!/usr/bin/env python3
"""Focused actual-publisher tests; no device, kernel or external build."""
import pathlib, subprocess
P=pathlib.Path(__file__).resolve().parent
R=P.parents[4]
B=P/'_build';B.mkdir(exist_ok=True)
cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections','-I',str(R/'shim/nord2-xnu'),'-I',str(P)]
cmd += [str(P/n) for n in ['test.c','wifi_station_transport.c','wifi_station_wire.c']]
cmd += [str(R/'shim/nord2-xnu'/n) for n in ['wifi_normal_transport.c','wifi_main_command.c']]
cmd += ['-Wl,--gc-sections','-o',str(B/'transport-tests')]
subprocess.run(cmd,check=True)
subprocess.run([str(B/'transport-tests')],check=True)
