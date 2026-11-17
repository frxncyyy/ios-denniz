from pathlib import Path
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());T=P/'proposed/tools';F=T/'fixtures/nord2-wifi-lifecycle';F.mkdir(parents=True,exist_ok=True)
(F/'owner.c').write_bytes((P/'tests/fixtures/nord2-wifi-rejoin/owner.c').read_bytes());(F/'test-power.inc').write_bytes((P/'tests/client/fixtures/nord2-wifi-ios-client/test-disconnect.inc').read_bytes())
s=(P/'tests/run.py').read_text().replace("F=P/'fixtures/nord2-wifi-rejoin'","F=P/'fixtures/nord2-wifi-lifecycle'");(T/'test-nord2-wifi-lifecycle.py').write_text(s)
# Reuse exact canonical IOUC build/source/lifetime machinery; only this focused
# suite's native power double and main are substituted in temporary files.
runner='''from pathlib import Path
import argparse,subprocess,tempfile
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());a=argparse.ArgumentParser();a.add_argument('--candidate',type=Path,default=R/'shim/nord2-xnu');a.add_argument('--log',type=Path,required=True);x=a.parse_args()
s=(R/'tools/test-nord2-wifi-ios-client.py').read_text();s=s.replace("R=next(x for x in Path(__file__).resolve().parents if(x/'AGENTS.md').exists());P=Path(__file__).resolve().parent/'fixtures/nord2-wifi-ios-client';",'R=Path('+repr(str(R))+');P=R/"tools/fixtures/nord2-wifi-ios-client";')
s=s.replace("(P/'test-disconnect.inc').read_text()",repr((P/'fixtures/nord2-wifi-lifecycle/test-power.inc').read_text()))
needle="(d/'test.cpp').write_text(source)"
prefix='static unsigned logical_power=1,power_calls;static uint64_t logical_epoch=1;\\nextern "C" int nord2_wifi_power_set(unsigned on){assert(on<=1);power_calls++;if(on!=logical_power)logical_epoch++;logical_power=on;return 0;}\\n'
injection="source=source.replace('extern \\\"C\\\" int nord2_wifi_scan_info',"+repr(prefix)+"+'extern \\\"C\\\" int nord2_wifi_scan_info').replace('out->mac[0]=2;','out->powered=logical_power;out->power_epoch=logical_epoch;out->mac[0]=2;');"
s=s.replace(needle,injection+needle)
with tempfile.TemporaryDirectory(prefix='lifecycle-client-')as t:
 f=Path(t)/'run.py';f.write_text(s);subprocess.run(['python3',str(f),'--candidate',str(x.candidate.resolve()),'--only','disconnect','--log',str(x.log.resolve())],check=True)
'''
(T/'test-nord2-wifi-lifecycle-client.py').write_text(runner)
