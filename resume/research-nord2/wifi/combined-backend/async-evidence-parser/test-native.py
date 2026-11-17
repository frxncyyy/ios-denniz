from pathlib import Path
import importlib.util,re,json,hashlib
p=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('parser',p/'parse-backend.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
main=''.join(l+'\n' for l in (p.parent/'log-parser/v3/synthetic-combined-success.log').read_text().splitlines() if l.startswith('NORD2 WIFI MAIN'));reports=[]
for f in [p.parent/'basic-sleepy-race/verification/full/full-native.log',p.parent/'basic-sleepy-race/verification/sleepy/sleepy-native.log']:
 count=0
 for b in re.findall(r'NORD2 WIFI CAP ARM:.*?NORD2 WIFI CAP WORKER:[^\n]*\n',f.read_text(),re.S):
  if b.count('NORD2 WIFI CAP ARM:')!=1 or b.count('NORD2 WIFI NORMAL COMMAND:')!=22 or 'MAIN DRAIN:' in b or 'NORD2 WIFI BACKEND: status=-' in b:continue
  r=m.parse((main+b).encode());assert r['terminal_receipt_valid'],r;count+=1
 assert count
 reports.append({'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'accepted_clean_blocks':count})
(p/'actual-fixture-report.json').write_text(json.dumps({'fixtures':reports,'hardware_claim':False,'source':'Actual native owner synthetic emitter output, fixed prior actual MAIN seed'},indent=2)+'\n');print('PASS actual emitter clean blocks:',[x['accepted_clean_blocks'] for x in reports])
