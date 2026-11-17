from pathlib import Path
import tempfile,json,subprocess,hashlib,shutil
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if p.name=='ios-sweet')
rows=json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text());row=next(x for x in rows if x['name']=='sweet_touch');cmd=row['argv'];source=Path(cmd[cmd.index('-c')+1]);out=P/'owner.arm64.o'
h=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
with tempfile.TemporaryDirectory(prefix='nse-owner-') as td:
 d=Path(td);shutil.copy2(source,d/'sweet_touch.c')
 dependencies={}
 for f in (R/'shim/nord2-xnu').iterdir():
  if f.suffix in ('.h','.inc'):shutil.copy2(f,d/f.name);dependencies[str(f.relative_to(R))]=h(f)
 for f in (P/'proposed').iterdir():shutil.copy2(f,d/f.name)
 actual=cmd.copy();actual[actual.index('-c')+1]=str(d/'sweet_touch.c');actual[actual.index('-o')+1]=str(out)
 result=subprocess.run(actual,cwd=row['cwd'],capture_output=True,text=True)
 (P/'compile-owner.log').write_text(result.stdout+result.stderr)
 record={'baseline_command_path':'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json','baseline_source':str(source.relative_to(R)),'baseline_source_sha256':h(source),'original_argv':cmd,'actual_argv':actual,'cwd':row['cwd'],'overlay_dependencies':dependencies,'proposed':{f.name:h(f) for f in (P/'proposed').iterdir()},'returncode':result.returncode,'qualification':'Prior root-recorded normal-context flags/cwd unchanged; only owning source/output substituted; source sibling headers/binders copied into temporary private overlay and three proposed files override them. No forced NORD2 or extra include paths. Not CI or linked/runtime proof.'}
 if not result.returncode:
  nm=subprocess.run([str(R/'../toolchains/proton-clang/bin/llvm-nm'),str(out)],capture_output=True,text=True,check=True);(P/'owner-symbols.txt').write_text(nm.stdout);record['nse_symbol_present']='_nord2_wifi_scan_boot_view' in nm.stdout;assert record['nse_symbol_present']
 (P/'compile-owner.json').write_text(json.dumps(record,indent=2)+'\n');print(result.returncode);assert not result.returncode,result.stderr[-2000:]
