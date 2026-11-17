#!/usr/bin/env python3
"""Selected CI flags/headers + exact private candidate. No generator/build/IO."""
from pathlib import Path
import hashlib,json,shlex,shutil,subprocess,tarfile
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').is_file());CI=R/'research/ci-39830672617';OUT=P/'compiled-review';OUT.mkdir(exist_ok=False);D=OUT/'inputs';D.mkdir();S=P/'proposed/shim/nord2-xnu'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
run=json.loads((CI/'run.json').read_text());assert run['headSha']=='1a4b1fed3d36622b9697085c55640dbd5f4595eb'
archive=CI/'xnu-buildtree/buildtree.tgz'
with tarfile.open(archive) as tf:
 members=tf.getmembers();assert all(not m.issym() and not m.islnk() and (D/m.name).resolve().is_relative_to(D) for m in members);tf.extractall(D,members=members,filter='data')
audit=json.loads((CI/'source-buildtree-audit.json').read_text());assert audit['passed'];generated={};overlay={}
for row in audit['files']:
 f=D/row['member'];assert sha(f)==row['sha256'];generated[row['member']]=sha(f)
 sibling=S/f.name
 if sibling.is_file():
  base=(R/'shim/nord2-xnu'/f.name).read_bytes();body=sibling.read_bytes();old=f.read_bytes();guard=b'#include <arm64/sweet_early.h>\n#if defined(NORD2)\n';tail=b'\n#endif /* NORD2 */\n'
  assert old in (base,guard+base+tail),f
  f.write_bytes(body if old==base else guard+body+tail);overlay[f.name]={'base_sha256':hashlib.sha256(base).hexdigest(),'candidate_sha256':sha(sibling),'generated_sha256':sha(f)}
assert set(overlay)=={p.name for p in S.iterdir()};dump(OUT/'source-pins.json',{'selected_ci':39830672617,'source':run['headSha'],'archive_sha256':sha(archive),'generated_ci':generated,'overlay':overlay})
units=['wifi_station_transport','wifi_station_transaction','wifi_wpa2','wifi_wpa_owner','wifi_wpa_nst','sweet_touch'];commands={};log=(CI/'build.log').read_text()
for line in log.splitlines():
 if ' -c ' not in line or '/Applications/' not in line:continue
 for name in units:
  if '/xnu/bsd/dev/'+name+'.c' in line:
   assert name not in commands;commands[name]=shlex.split(line[line.index('/Applications/'):])
assert set(commands)==set(units)
prefix='/Users/runner/work/ios-sweet/ios-sweet/dxb/'
sdk='/Applications/Xcode_15.0.1.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX14.0.sdk//System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/platform'
records=[]
for name in units:
 original=commands[name];cwd=D/'build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT';argv=[shutil.which('clang'),'-target','arm64-apple-macos13.0','-I'+str(D/'xnu/osfmk')];i=1
 while i<len(original):
  a=original[i]
  if a in ('-arch','-o','-MF'):
   val=original[i+1]
   if a=='-arch':assert val=='arm64'
   elif a=='-o':argv.extend([a,str(OUT/(name+'.o'))])
   else:argv.extend([a,str(OUT/(name+'.d'))])
   i+=2;continue
  argv.append(a.replace(prefix,str(D)+'/').replace(sdk,str(D/'sdk-kernel-platform')));i+=1
 if '-o' not in argv:argv.extend(['-o',str(OUT/(name+'.o'))])
 assert '-mcpu=generic' in argv and '-march=armv8.2-a+crypto' in argv
 result=subprocess.run(argv,cwd=cwd,capture_output=True);(OUT/(name+'.log')).write_bytes(result.stdout+result.stderr)
 row={'name':name,'exit':result.returncode,'original_argv':original,'argv':argv,'cwd':str(cwd)}
 if not result.returncode:
  row['object_sha256']=sha(OUT/(name+'.o'));deps=(OUT/(name+'.d')).read_text().replace('\\\n',' ').split(':',1)[1].split();row['dependencies']={str(p.resolve()):sha(p) for x in deps if (p:=Path(x) if Path(x).is_absolute() else cwd/x).is_file()}
 records.append(row);print(name,result.returncode,flush=True)
 if result.returncode:print(result.stderr.decode()[-3000:]);break
passed=len(records)==len(units) and all(x['exit']==0 for x in records)
dump(OUT/'report.json',{'passed':passed,'compiler':subprocess.check_output(['clang','--version'],text=True),'source_pins_sha256':sha(OUT/'source-pins.json'),'commands':records,'limitations':['Local Clang replaces Apple Clang; target/path substitutions as recorded.','Exact prior successful CI SDK/generated headers; 9 candidate siblings are overlaid preserving wrappers.','C TUs only, no Apple builtin substitutions or extra warning relaxations.','Compilation is not linked/firmware/RF/connection validation.']});assert passed
