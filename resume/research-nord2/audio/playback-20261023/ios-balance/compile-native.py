from pathlib import Path
import json,subprocess,hashlib,sys
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if(p/'AGENTS.md').exists())
D=P/'native';D.mkdir(exist_ok=False)
templates=json.loads((P.parent/'ios-clocks/native/results.json').read_text())
t=next(x for x in templates if x['name']=='audio_device')
results=[]
for name in ('audio_i2c8','audio_id','audio_hw','audio_device','audio_stream','audio_live','audio_amp','audio_board','audio_afe','sweet_touch','pe_fiq'):
 argv=list(t['argv'])
 source=R/'research/ci-39158169212/buildtree/xnu'/('pexpert/arm' if name=='pe_fiq' else 'bsd/dev')/(name+'.c')
 argv=[str(source) if x.endswith('/bsd/dev/audio_device.c') else x for x in argv]
 if name=='pe_fiq':argv.append('-DPEXPERT_KERNEL_PRIVATE')
 argv[argv.index('-o')+1]=str(D/(name+'.o'))
 with(D/(name+'.log')).open('xb')as log:code=subprocess.run(argv,cwd=t['cwd'],stdout=log,stderr=subprocess.STDOUT).returncode
 results.append({'name':name,'exit':code,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'cwd':t['cwd'],'argv':argv,'removed_duplicate_defines':t.get('removed_duplicate_defines',[])})
 (D/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 print(name,code,flush=True)
 if code:sys.exit(code)
