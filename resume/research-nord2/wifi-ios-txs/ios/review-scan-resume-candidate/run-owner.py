#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess
D=Path(__file__).resolve().parent;R=next(p for p in D.parents if(p/'AGENTS.md').exists());a=argparse.ArgumentParser();a.add_argument('--output',type=Path,default=D/'verification');args=a.parse_args();W=args.output.resolve();W.mkdir(parents=True,exist_ok=True)
s=(R/'tools/test-nord2-wifi-join-owner.py').read_text();start=s.index(" test+='''");end=s.index(" (d/'test.c').write_text(test)",start)
s=s[:start]+" test+="+repr((D/'tests/owner-extra.c').read_text())+'\n'+s[end:]
s=s.replace("P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists());",'R=Path('+repr(str(R))+');P=R/"tools";')
# Model actual station command/frame DMA retirement, retaining real ownership.
s=s.replace(" # Existing full native fixture's probes", " owner=(d/'owner-fixture.inc').read_text().replace('static unsigned normal_enabled,','static unsigned station_hardware;static void station_completion(unsigned);\\nstatic unsigned normal_enabled,').replace('if(normal_enabled&&t->completed>=5){','if(normal_enabled&&t->completed>=5){if(station_hardware){station_completion(next);return;}')\n (d/'owner-fixture.inc').write_text(owner)\n # Existing full native fixture's probes")
s=s.replace("with tempfile.TemporaryDirectory(prefix='nss-native-') as td:","with tempfile.TemporaryDirectory(prefix='nss-native-') as td:")
s=s.replace("result=subprocess.run([str(d/'test'),","(Path("+repr(str(W))+")/'test-native.elf').write_bytes((d/'test').read_bytes());result=subprocess.run([str(d/'test'),")
f=W/'runner.py';f.write_text(s);subprocess.run(['python3',str(f),'--candidate',str(D/'proposed/shim/nord2-xnu'),'--log-dir',str(W)],check=True)
