#!/usr/bin/env python3
"""Closed, synthetic-only stage checks. Never invoke builder/runner/device."""
from pathlib import Path
import ast,hashlib,json,subprocess,sys,tempfile,shutil
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').is_file());O=R/'research/nord2/wifi-ios-dhcp/ios'
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for p in P.rglob('*.py'):ast.parse(p.read_text())
subprocess.run(['sh','-n',str(P/'build-image.sh')],check=True)
subprocess.run([sys.executable,'-B',str(P/'closed-preflight-test.py')],check=True,cwd=R)
mod=ast.parse((P/'verify-image.py').read_text());values={}
for n in mod.body:
 if isinstance(n,ast.Assign):
  try:values[n.targets[0].id]=ast.literal_eval(n.value)
  except (AttributeError,ValueError):pass
cmd=values['BASE_COMMANDLINE']+' '+values['NEW_BOOT_ARG'];assert len(cmd.encode())==555 and cmd.endswith(' nord2wifiios=1 nord2wifilive=1')
assert len(cmd.split())==len(set(cmd.split())) and len(cmd.encode())<608
assert read(P/'recovery-image.json')==read(O/'recovery-image.json')
old=ast.parse((O/'collect-snapshot.py').read_text());new=ast.parse((P/'collect-snapshot.py').read_text())
def commands(tree):return next(n.value for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='commands' for t in n.targets))
a=commands(old);b=commands(new);assert len(a.keys)==7 and len(b.keys)==8
assert [ast.dump(k) for k in a.keys]==[ast.dump(k) for k in b.keys[:-1]] and [ast.dump(v) for v in a.values]==[ast.dump(v) for v in b.values[:-1]]
# A private copied stage exercises all actual read-only parsers. Its named
# snapshots are synthetic and are never mixed into future hardware evidence.
W=P/'review-template-tests';W.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='synthetic-',dir=W) as td:
 d=Path(td)
 for n in ('collect-snapshot.py','reader-dependencies.json','parse-backend.py','parse-ios-service.py','parse-connection.py','core-receipt.py','core-lifecycle.py','parser-dependency.json'):(d/n).write_bytes((P/n).read_bytes())
 n='extended-reader/proposed/tools/nord2-wifi-data-extended.py';(d/n).parent.mkdir(parents=True);(d/n).write_bytes((P/n).read_bytes())
 samples={'good':'NORD2 WIFI TXPUB: g=1 pid=64 wlan=5 kind=3 protected=1 ether=2048 dhcp=1 parse=1 bytes=342 class=0 pub=7\n','bad':'NORD2 WIFI TXPUB: g=1 pid=64 wlan=5 kind=3 protected=1 ether=2048 dhcp=1 parse=1 bytes=342 class=0 pub=7 unknown=1\n'}
 results={}
 for name,s in samples.items():
  f=d/(name+'.txt');f.write_text(s);p=subprocess.run([sys.executable,'-B',str(d/'collect-snapshot.py'),'synthetic-'+name,str(f)],cwd=R,capture_output=True,text=True);assert not p.returncode,p.stdout+p.stderr
  m=json.loads(p.stdout);assert len(m['parser_exit_codes'])==8;assert m['parser_exit_codes']['tx-correlation']==(0 if name=='good' else 1);results[name]=m['parser_exit_codes']
(P/'template-report.json').write_text(json.dumps({'passed':True,'stage_closed':True,'syntax_and_shell_syntax':True,'commandline_bytes':555,'committed_source_known_CI_image_approval_null':True,'runner_unused':True,'no_device_build_CI_actions':True,'old_seven_reader_calls_exact_AST':True,'additive_reader_wiring':results,'recovery_unchanged':True,'synthetic_only':True,'future_artifact_verified':False},indent=2)+'\n')
print('PASS closed fresh stage,555B commandline, recovery pins, eight-reader wiring good/bad; synthetic only')
