from pathlib import Path
import json,re
P=Path(__file__).resolve().parent;lines=(P/'wifid-disassembly.txt').read_text().splitlines();names={x['id']:x['name'] for x in json.loads((P/'request-names-all.json').read_text())};out=[]
def const_before(i,reg):
 for line in reversed(lines[max(0,i-16):i]):
  if re.search(r'\bbl\b|\bblr\b',line):break
  m=re.search(r'\bmov\s+w'+str(reg)+r', #(\d+)',line)
  if m:return int(m[1])
  if re.search(r'\b(?:mov|ldr|ldur|add|sub|orr|and|mvn|csel)\s+[wx]'+str(reg)+r',',line):return None
 return None
for i,l in enumerate(lines):
 if re.search(r'\bbl\s+0x1001750b4\b',l):
  req=const_before(i,1);extent=const_before(i,4);out.append({'call_va':l.split(':')[0],'direct_request_constant':req,'request_name':names.get(req),'direct_output_extent_constant':extent,'limits':'local basic-block constant observation only; unknown is not zero and this is not complete callgraph/order'})
(P/'direct-get-calls.json').write_text(json.dumps(out,indent=2)+'\n');print('directGet calls',len(out))
for x in out:print(x['call_va'],x['direct_request_constant'],x['request_name'],x['direct_output_extent_constant'])
