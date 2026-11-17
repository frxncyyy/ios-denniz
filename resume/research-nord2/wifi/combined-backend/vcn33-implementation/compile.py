from pathlib import Path
import json,tempfile,shutil,subprocess
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if x.name=='ios-sweet');S=P/'proposed/shim/nord2-xnu';x=next(x for x in json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text()) if x['name']=='sweet_touch')
with tempfile.TemporaryDirectory(prefix='nord2-vcn33-native-') as t:
 d=Path(t);(d/'bsd/dev').mkdir(parents=True);src=R/'research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c';shutil.copy(src,d/'bsd/dev/sweet_touch.c')
 subprocess.run(['patch','-p1','-i',str(P/'sweet_touch.patch')],cwd=d,check=True,capture_output=True)
 for f in (R/'shim/nord2-xnu').glob('*.inc'):shutil.copy(f,d/'bsd/dev'/f.name)
 for f in S.glob('*.inc'):shutil.copy(f,d/'bsd/dev'/f.name)
 cmd=x['argv'][:];cmd.insert(1,'-I'+str(S));cmd[cmd.index('-c')+1]=str(d/'bsd/dev/sweet_touch.c');cmd[cmd.index('-o')+1]=str(d/'sweet_touch.o');z=subprocess.run(cmd,cwd=x['cwd'],capture_output=True,text=True);print("\n".join(a for a in z.stderr.splitlines() if "error:" in a));
 if z.returncode:raise SystemExit(z.returncode)
 print('PASS selected normal-context ARM64 sweet_touch candidate')
