from pathlib import Path
import subprocess,tempfile,runpy
P=Path(__file__).resolve().parent;D=P/'proposed/shim/nord2-xnu'
with tempfile.TemporaryDirectory(prefix='nstd-') as t:
 for name in ['test','test-budget']:
  cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(D),str(P/(name+'.c'))]
  if name=='test':cmd+=[str(D/'wifi_station_diag.c')]
  cmd+=['-o',str(Path(t)/name)];subprocess.run(cmd,check=True);z=subprocess.run([str(Path(t)/name)],check=True,capture_output=True,text=True);(P/(name+'.log')).write_text(z.stdout+z.stderr);print(z.stdout.strip())
parse=runpy.run_path(str(P/'parse.py'))['parse'];base='NORD2 WIFI NSTDIAG: n=1 g=1 t=2 k=5 a=99 b=2 c=3 d=138 e=1 f=95 h=1 i=0'
assert parse(base)['m1_public_class']==1
neg=[base+' secret=0',base.replace('n=1','n=0'),base.replace('k=5','k=7'),base.replace('e=1','e=2'),base.replace('h=1','h=2'),base.replace('g=1','g=18446744073709551616'),base+'\n'+base,base.replace('i=0','i=1')]
for x in neg:
 try:parse(x)
 except ValueError:pass
 else:raise AssertionError(x)
(P/'parser-test.log').write_text('PASS parser1positive8negative; no readiness inference\n');print('PASS parser1positive8negative')
