#!/usr/bin/env python3
import pathlib,subprocess
P=pathlib.Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'AGENTS.md').exists())
B=P/'_build';B.mkdir(exist_ok=True)
S=P/'proposed/shim/nord2-xnu'
cmd=['clang','-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(S),'-I'+str(R/'shim/nord2-xnu'),str(S/'wifi_ios_join.c'),str(P/'tests/test-join.c'),'-o',str(B/'test-join')]
subprocess.run(cmd,check=True);subprocess.run([str(B/'test-join')],check=True)

cmd=['clang','-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(S),'-I'+str(R/'shim/nord2-xnu'),str(S/'wifi_ios_join.c'),str(S/'wifi_ios_station_view.c'),*[str(R/'shim/nord2-xnu'/('wifi_ios_'+n+'.c')) for n in ('policy','mapping','rates','scan_contract')],str(P/'tests/test-view.c'),'-o',str(B/'test-view')]
subprocess.run(cmd,check=True);subprocess.run([str(B/'test-view')],check=True)
