#!/usr/bin/env python3
"""Run only existing-image offline reviews sequentially after parent notification."""
from pathlib import Path
import json,subprocess,sys
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if(p/'AGENTS.md').is_file())
S=R/'research/nord2/audio/playback-20261023/ios-clocks'
b=json.loads((S/'build-result.json').read_text())
assert b['source_commit']=='428e55d47777febe1c97016e9ee1525f6bd91855' and b['ci_run']==39998991055
assert b['hal_package_sha256']=='82253445dad22fee379af4c141d6883aa91e322322bbe38dd62959a34af8b4aa'
C=R/'research/ci-39998991055'
for role in ('playback','rollback'):
 entries=[e for e in b['images'] if e['role']==role];assert len(entries)==1;e=entries[0]
 output=P/('clocks-'+role);assert not output.exists()
 with(P/('clocks-'+role+'-result.txt')).open('x')as log:
  subprocess.run([sys.executable,str(P/'verify.py'),'--stage',str(S),'--image',str(R/e['image']),
   '--payload',str(R/e['payload']),'--kernel',str(C/'xnu-kernel/kernel.development.sweet'),
   '--plugin-artifact',str(C/'nord2-audio-hal'),'--output',str(output),'--mode',role,
   '--ci-run',str(b['ci_run']),'--source',b['source_commit'],'--attempt',str(b['ci_attempt'])],cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
 report=json.loads((output/'report.json').read_text())
 print(json.dumps({k:report[k]for k in('verdict','mode','source','ci_run','image_sha256','image_bytes','boot_spare_bytes','hardware_tested')}),flush=True)
