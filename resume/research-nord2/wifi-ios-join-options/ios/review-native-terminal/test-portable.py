#!/usr/bin/env python3
from pathlib import Path
import tempfile,shutil,sys,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists())
with tempfile.TemporaryDirectory(prefix='nst-portable-fixtures-') as tmp:
 f=Path(tmp)
 for p in(R/'tools/fixtures/nord2-wifi-station').iterdir():
  if p.is_file():shutil.copy2(p,f/p.name)
 for name in ['model-test.c','test-native-air-timeout.c']:shutil.copy2(P/'proposed/tools/fixtures/nord2-wifi-station'/name,f/name)
 s=(P/'proposed/tools/test-nord2-wifi-station.py').read_text().replace("P=Path(__file__).resolve().parent/'fixtures/nord2-wifi-station'","P=Path("+repr(str(f))+")")
 exec(compile(s,str(R/'tools/test-nord2-wifi-station.py'),'exec'),{'__file__':str(R/'tools/test-nord2-wifi-station.py'),'__name__':'__main__'})
