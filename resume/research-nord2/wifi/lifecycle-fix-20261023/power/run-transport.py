from pathlib import Path
import subprocess,argparse
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists());a=argparse.ArgumentParser();a.add_argument('--candidate',type=Path,default=P/'proposed/shim/nord2-xnu');a.add_argument('--log-dir',type=Path,required=True);x=a.parse_args();x.log_dir.mkdir(parents=True,exist_ok=True)
s=(R/'tools/test-nord2-wifi-station.py').read_text();s=s.replace("R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists())",'R=Path('+repr(str(R))+')');s=s.replace("'airtimeout':('test-native-air-timeout.c',","'airtimeout':("+repr(str(P/'tests/test-deauth.c'))+",")
s=s.replace("P=Path(__file__).resolve().parent/'fixtures/nord2-wifi-station'",'P=R/"tools/fixtures/nord2-wifi-station"')
f=x.log_dir/'run.py';f.write_text(s);subprocess.run(['python3',str(f),'--candidate',str(x.candidate.resolve()),'--only','airtimeout','--log-dir',str(x.log_dir.resolve())],check=True)
