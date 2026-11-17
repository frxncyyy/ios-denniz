#!/usr/bin/env python3
"""Run only existing-image offline reviews sequentially after parent notification."""
from pathlib import Path
import json,subprocess,sys,hashlib,argparse
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if(p/'AGENTS.md').is_file())
S=R/'research/nord2/audio/playback-20261023/ios-gainrange'
ap=argparse.ArgumentParser();ap.add_argument('--source',required=True);ap.add_argument('--ci-run',required=True,type=int);args=ap.parse_args()
b=json.loads((S/'build-result.json').read_text())
assert len(args.source)==40 and all(c in '0123456789abcdef' for c in args.source)
assert b['source_commit']==args.source and b['ci_run']==args.ci_run
assert b['hal_package_sha256']=='82253445dad22fee379af4c141d6883aa91e322322bbe38dd62959a34af8b4aa'
C=R/('research/ci-'+str(args.ci_run))
def file_sha(path):
 h=hashlib.sha256()
 with path.open('rb')as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
preferred='3b8cb873267d2fa4b6fd53bb907229c1297e1761eaf1dce5116b197d4bec4c46'
fallback_receipt=json.loads((S/'fallback-verification.json').read_text())
assert fallback_receipt['checked_offline'] is True
fallback=next(e for e in fallback_receipt['fallbacks'] if e['sha256']==preferred)
fallback_path=R/fallback['path']
assert fallback_path.stat().st_size==fallback['bytes']==33540096
assert file_sha(fallback_path)==preferred
for role in ('playback','rollback'):
 entries=[e for e in b['images'] if e['role']==role];assert len(entries)==1;e=entries[0]
 output=P/('gainrange-'+role);assert not output.exists()
 with(P/('gainrange-'+role+'-result.txt')).open('x')as log:
  subprocess.run([sys.executable,str(P/'verify.py'),'--stage',str(S),'--image',str(R/e['image']),
   '--payload',str(R/e['payload']),'--kernel',str(C/'xnu-kernel/kernel.development.sweet'),
   '--plugin-artifact',str(C/'nord2-audio-hal'),'--output',str(output),'--mode',role,
   '--ci-run',str(b['ci_run']),'--source',b['source_commit'],'--attempt',str(b['ci_attempt'])],cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
 assert file_sha(fallback_path)==preferred
 report=json.loads((output/'report.json').read_text())
 report['preferred_fallback_sha256']=preferred
 report['preferred_fallback_verified']=True
 (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k]for k in('verdict','mode','source','ci_run','image_sha256','image_bytes','boot_spare_bytes','hardware_tested')}),flush=True)
