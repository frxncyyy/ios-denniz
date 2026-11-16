import json,collections,sys,ast
rows=[json.loads(l) for l in open(sys.argv[1])]
c=collections.Counter();ex={}
for r in rows:
  for t in ('creation','region'):
    if r.get(t+'_abi')!='differs': continue
    a=r[t+'_abi_air'];b=r[t+'_abi_m2v'];a['manifest']=[tuple(x) for x in a.get('manifest',[])];b['manifest']=[tuple(x) for x in b.get('manifest',[])];a['local']=tuple(a['local']);b['local']=tuple(b['local'])
    keys=[]
    for k in sorted(set(a)|set(b)):
      if a.get(k)!=b.get(k):
        if k=='manifest':
          ma=set(a['manifest']);mb=set(b['manifest'])
          d=[]
          for x in sorted(ma-mb): d.append('air+'+str(x))
          for x in sorted(mb-ma): d.append('m2v+'+str(x))
          # classify
          kinds=set()
          for x in ma^mb: kinds.add((x[2],))
          keys.append('manifest:'+ ('count' if len(ma)!=len(mb) else 'rec')+':kinds'+str(sorted(kinds)))
        elif k=='local':
          keys.append('local:%s/%s'%(a[k],b[k]) if t=='creation' else 'local' + ('(air=region)' if tuple(a[k])==(8,8,1) else '(air!=region)'))
        else: keys.append('%s'%k)
    key=(t,tuple(keys));c[key]+=1;ex.setdefault(key,(r['name'],str(r[t+'_abi_air'])[:500],str(r[t+'_abi_m2v'])[:500]))
for k,v in c.most_common(): print(v,k); print('    ',ex[k][0]); print('    air',ex[k][1]); print('    m2v',ex[k][2])
