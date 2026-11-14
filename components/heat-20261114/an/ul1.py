import json,sys,collections,re
procs=collections.Counter();cd=collections.Counter();boots=collections.Counter();tmin={};tmax={}
out=open('coreduetd.jsonl','w'); n=0
for l in open('n2log.jsonl',errors='replace'):
    n+=1
    try: j=json.loads(l)
    except Exception: continue
    p=(j.get('process') or '').split('/')[-1]; procs[p]+=1
    b=(j.get('boot_uuid') or '')[:8]; boots[b]+=1
    t=j.get('timestamp') or ''; 
    if b not in tmin or t<tmin[b]: tmin[b]=t
    if b not in tmax or t>tmax[b]: tmax[b]=t
    if p in('coreduetd','dasd','duetexpertd','contextstored','biomed','knowledge-agent'):
        out.write(json.dumps({'t':t,'b':b,'p':p,'pid':j.get('pid'),'tid':j.get('thread_id'),'s':j.get('subsystem'),'c':j.get('category'),'lt':j.get('log_type'),'m':(j.get('message') or '')[:1500],'f':(j.get('format_string') or '')[:300]})+'\n')
    if p=='coreduetd': cd[(j.get('subsystem'),(j.get('format_string') or '')[:110])]+=1
print('lines',n)
for b,c in boots.most_common(): print('boot',b,c,tmin[b],tmax[b])
for p,c in procs.most_common(40): print(f'{c:8d} {p}')
print('--- coreduetd formats')
for k,c in cd.most_common(40): print(f'{c:7d} {k}')
