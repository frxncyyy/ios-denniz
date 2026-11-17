#!/usr/bin/env python3
"""No device: execute actual collector with six existing readers stubbed only.
The seventh RXERROR invocation is the real pinned canonical subprocess."""
from pathlib import Path
import ast,contextlib,hashlib,io,json,subprocess,sys,tempfile
D=Path(__file__).resolve().parent.parent;R=next(p for p in D.parents if(p/'AGENTS.md').is_file());W=Path(__file__).resolve().parent
source=(D/'collect-snapshot.py').read_text();old=(R/'research/nord2/wifi-ios-datapath/ios/collect-snapshot.py').read_text()
def commands(s):
 t=ast.parse(s);return next(n.value for n in t.body if isinstance(n,ast.Assign) and any(isinstance(k,ast.Name) and k.id=='commands' for k in n.targets))
a,b=commands(source),commands(old)
assert [k.value for k in a.keys]==[k.value for k in b.keys]+['rx-error']
assert [ast.dump(x) for x in a.values[:6]]==[ast.dump(x) for x in b.values]
real_run=subprocess.run;original_argv=sys.argv[:];reports=[]
line='NORD2 WIFI RXERROR: g=1 n=1 d1=8388608 d2=0 d4=0 wlan=5 sec=4 key=0 ht=1 groups=9 type=2 a1=1 dst=1 tid=0\n'
try:
 for case,text,exitcode in [('positive',line,0),('malformed',line.replace('d1=8388608','d1=1'),1)]:
  with tempfile.TemporaryDirectory(prefix='wiring-',dir=W) as td:
   p=Path(td);(p/'collect-snapshot.py').write_text(source);(p/'reader-dependencies.json').write_bytes((D/'reader-dependencies.json').read_bytes());input=p/'input.log';input.write_text(text)
   calls=[]
   def invoke(args,**kwargs):
    calls.append(args)
    if len(calls)<=6:return subprocess.CompletedProcess(args,16+len(calls))
    assert Path(args[1])==R/'tools/nord2-wifi-rx-error.py'
    return real_run(args,**kwargs)
   subprocess.run=invoke;sys.argv=[str(p/'collect-snapshot.py'),case,str(input)]
   with contextlib.redirect_stdout(io.StringIO()):exec(compile(source,str(p/'collect-snapshot.py'),'exec'),{'__file__':str(p/'collect-snapshot.py'),'__name__':'__main__'})
   subprocess.run=real_run
   pin=json.loads((p/'hardware-review'/f'{case}-pin.json').read_text());out=json.loads((p/'hardware-review'/f'{case}-rx-error.json').read_text())
   assert len(calls)==7 and list(pin['parser_exit_codes'])==['backend','ios','connection','wpa','data','extended','rx-error']
   assert list(pin['parser_exit_codes'].values())==list(range(17,23))+[exitcode]
   assert out['valid']==(case=='positive')
   assert pin['bytes']==len(text.encode()) and pin['sha256']==hashlib.sha256(text.encode()).hexdigest()
   reports.append({'case':case,'rx_error_exit':exitcode,'rx_error_valid':out['valid'],'all_seven_exit_codes_preserved':True,'six_existing_calls_unchanged':True,'real_pinned_rx_reader_invoked':True})
finally:subprocess.run=real_run;sys.argv=original_argv
report={'passed':True,'cases':reports,'scope':'Collector wiring only: existing six reader subprocesses intentionally stubbed with distinct failure codes; canonical RXERROR subprocess actual. No image/gates/runtime/USB/device invoked.','pins':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (D/'collect-snapshot.py',D/'reader-dependencies.json',D/'README.md',R/'tools/nord2-wifi-rx-error.py')}}
(W/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
