import sys,json
for l in sys.stdin:
  if l.startswith('built'):print(l.strip());continue
  n,_,j=l.partition(' ')
  try:d=json.loads(j)
  except Exception:print(l[:300].rstrip());continue
  print(n,'full',d['full'],d['full_draws'],'cover',d['full_cover'],'other',d['other'],{k:v for k,v in d['breaks'].items() if k in('target','maxdraw','rdtgt','fit','end','clear','preclear','mrt','shape','spans')})
