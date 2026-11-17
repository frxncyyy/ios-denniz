#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess
R=next(p for p in Path(__file__).resolve().parents if(p/'AGENTS.md').exists());D=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--baseline',action='store_true');ap.add_argument('--only',choices=('client','service'));a=ap.parse_args()
W=D/('verification-old' if a.baseline else 'verification');W.mkdir(exist_ok=True)
C=D/'baseline' if a.baseline else D/'proposed/shim/nord2-xnu'
if a.only!='service':
 script=(R/'tools/test-nord2-wifi-ios-client.py').read_text();script=script[:script.index(' logs=[]')]
 script=script.replace("P=Path(__file__).resolve().parent/'fixtures/nord2-wifi-ios-client'","P=R/'tools/fixtures/nord2-wifi-ios-client'")
 script=script.replace("(P/'test-extra.inc').read_text()",repr((D/'tests/test-disconnect.inc').read_text()))
 script+='''\n subprocess.run(['clang++','-std=c++17',*flags,'-Wno-unused-function','-pthread',str(d/'test.cpp'),*objs,'-o',str(d/'test')],check=True)\n result=subprocess.run([str(d/'test')],text=True,capture_output=True);print(result.stdout);print(result.stderr)\n if args.log:args.log.write_text(result.stdout+result.stderr)\n result.check_returncode()\n'''
 f=W/'client-runner.py';f.write_text(script);subprocess.run(['python3',str(f),'--candidate',str(C),'--log',str(W/'client.log')],check=True)
if a.only!='client':
 script=(R/'tools/test-nord2-wifi-station.py').read_text().replace("P=Path(__file__).resolve().parent/'fixtures/nord2-wifi-station'","P=R/'tools/fixtures/nord2-wifi-station'")
 script=script.replace('files=[P/fixture]','files=[Path('+repr(str(D/'tests/service-test.c'))+')]')
 f=W/'service-runner.py';f.write_text(script);subprocess.run(['python3',str(f),'--candidate',str(C),'--only','service','--log-dir',str(W)],check=True)
