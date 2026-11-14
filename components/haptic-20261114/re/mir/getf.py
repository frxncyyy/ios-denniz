import sys,os,cat,pickle
fs=cat.load(); ent=cat.index(fs)
out=sys.argv[1]
for p in sys.argv[2:]:
    if p.endswith('/'):
        pre=p
        for cn,(pp,nm,rt,sz) in ent.items():
            pa=cat.path(ent,cn)
            if pa.startswith(pre) and rt==2:
                try:
                    d=cat.get(fs,pa); o=out+pa; os.makedirs(os.path.dirname(o),exist_ok=True); open(o,'wb').write(d); print(len(d),pa)
                except Exception as e: print('ERR',pa,e)
    else:
        try:
            d=cat.get(fs,p); o=out+p; os.makedirs(os.path.dirname(o),exist_ok=True); open(o,'wb').write(d); print(len(d),p)
        except Exception as e: print('ERR',p,e)
