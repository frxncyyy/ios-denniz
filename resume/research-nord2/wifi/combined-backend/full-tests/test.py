#!/usr/bin/env python3
import pathlib,subprocess,tempfile,hashlib,json
P=pathlib.Path(__file__).resolve().parent;R=next(x for x in P.parents if x.name=='ios-sweet');S=P.parent/'candidate/shim/nord2-xnu';C=R/'shim/nord2-xnu';board=R/'research/nord2/wifi/calibration/WIFI'
assert hashlib.sha256(board.read_bytes()).hexdigest()=='0000000000000000000000000000000000000000000000000000000000000000'
with tempfile.TemporaryDirectory(prefix='ncb-full-') as d:
 files=sorted(S.glob('*.c'))+[C/(n+'.c') for n in ('wifi_basic_resources','wifi_basic_metadata','wifi_basic_primitives','wifi_capability','wifi_capability_dispatcher','wifi_main_initializer','wifi_main_dmashdl','wifi_main_init_plan','wifi_main_transport','wifi_main_layout','wifi_dma_preflight','wifi_main_command','dma_pool_contract') if not (S/(n+'.c')).exists()]
 pins={str(x.relative_to(S)):hashlib.sha256(x.read_bytes()).hexdigest() for x in sorted(S.glob('*')) if x.is_file()}
 adapter=(S/'wifi_capability_bind.inc').read_text()
 adapter=adapter.replace('static int ncd_native_mread(void*c,uint64_t p,void*v,uint32_t n)\n{','static int ncd_native_mread(void*c,uint64_t p,void*v,uint32_t n)\n{full_test_probe(0);')
 adapter=adapter.replace('static int ncd_native_write(void*c,uint32_t p,uint32_t v)\n{','static int ncd_native_write(void*c,uint32_t p,uint32_t v)\n{full_test_probe(1);')
 import re
 for name in ('owner','read','write','mread','mwrite','sync','delay','now'):
  adapter,n=re.subn(r'(static int ncd_native_'+name+r'\([^)]*\)\s*\{)',r'\1\n#ifdef FULL_FAULTS\nif(full_test_fail())return -1;\n#endif\n',adapter,count=1);assert n==1,name
 (pathlib.Path(d)/'wifi_capability_bind.inc').write_text(adapter)
 logs=[]
 for name in ('full','faults'):
  exe=pathlib.Path(d)/name
  cmd=['clang','-std=c11','-DNCFG_HOST_TEST','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+d,'-I'+str(S),'-I'+str(C),str(P/('test-'+name+'.c'))]+list(map(str,files))+['-lcrypto','-o',str(exe)]
  subprocess.run(cmd,check=True);r=subprocess.run([str(exe),str(board)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(P/'test.log').write_text('\n'.join(logs))
  if r.returncode:print(r.stdout[-6000:]);print(r.stderr);r.check_returncode()
  print(r.stdout.splitlines()[-1])
 assert pins=={str(x.relative_to(S)):hashlib.sha256(x.read_bytes()).hexdigest() for x in sorted(S.glob('*')) if x.is_file()},'candidate changed during test'
 (P/'candidate-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
