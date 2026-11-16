import json,collections,sys
rows=[json.loads(l) for l in open(sys.argv[1])]
c=collections.Counter()
for r in rows:
  for t in ('creation','region'):
    c[(t,'air='+str(r.get(t+'_air')),'m2v='+str(r.get(t+'_m2v')),'abi='+str(r.get(t+'_abi')))]+=1
for k,v in sorted(c.items(),key=str): print(v,k)
e=collections.Counter((r.get('region_air_error') or r.get('creation_air_error') or r.get('error') or r.get('crash_log') or '')[:110] for r in rows if r.get('region_air')!='ok' and r.get('creation_air')!='ok')
print('-- refusals (neither ok)');[print(v,k) for k,v in e.most_common(60)]
