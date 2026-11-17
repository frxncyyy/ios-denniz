from pathlib import Path
import subprocess,json,hashlib
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file());P=Path(__file__).parent;N=R/'research/nord2/wifi-ios-local-submit/ios'
sha=lambda b:hashlib.sha256(b).hexdigest()
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()
old=json.loads((N/'source-proposals.json').read_text())
paths={x['path'] for x in old['source_pins']}
for path in subprocess.check_output(['git','diff-tree','--no-commit-id','--name-only','-r',head],cwd=R,text=True).splitlines():
 if path.startswith(('shim/nord2-xnu/','tools/','xnu/patches/')):paths.add(path)
pins=[]
for path in sorted(paths):
 b=(R/path).read_bytes();assert subprocess.check_output(['git','show',head+':'+path],cwd=R)==b,path
 pins.append({'path':path,'sha256':sha(b),'bytes':len(b)})
reviews=[]
for path in [P/'compiled-review/attempt1/report.json',P/'compiled-review/attempt1/source-pins.json',P/'independent-final-composition/review.json',P/'independent-final-composition/manifest.json',P/'station-tests/report.json',P/'normal-tests.json',P/'wpa-final-tests.log',P/'data-diag-tests/report.json',P/'scan-return-final-tests.log',P/'fixture-whitespace-normalization.json',P/'data-reader-final-tests/reader-report.json',N/'review-data-reader-independent/independent-review.json',N/'review-data-reader-v2/manifest.json']:
 assert path.is_file(),path
 b=path.read_bytes();reviews.append({'path':str(path.relative_to(R)),'sha256':sha(b),'bytes':len(b)})
meta={'source_commit':head,'source_verified':True,'runtime_scope':'Previously authorized clean scan return after complete retirement; bounded numeric WPA rejection/DHCP/TXS diagnosis without changes to crypto acceptance, descriptors, transport ownership or one-join policy; no IP connectivity claim.','reviews':reviews,'source_pins':pins}
(P/'source-proposals.json').write_text(json.dumps(meta,indent=2)+'\n')
print(json.dumps({'source_commit':head,'files':len(pins),'reviews':len(reviews),'manifest_sha256':sha((P/'source-proposals.json').read_bytes())}))
