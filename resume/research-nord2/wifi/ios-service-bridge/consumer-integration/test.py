from pathlib import Path
import subprocess,tempfile,json
P=Path(__file__).resolve().parent;M=P.parent/'passive-scan-contract/v2'
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'rate';cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(M),str(P/'niw_rate_compat.c'),str(M/'mapping.c'),str(P/'test-rate.c'),'-o',str(exe)]
 subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());old=json.loads((P.parent/'scan-backend-boundary/compile-command.json').read_text());cmd=old['argv'][:];cmd.insert(1,'-I'+str(M));cmd[cmd.index('-c')+1]=str(P/'niw_rate_compat.c');cmd[cmd.index('-o')+1]=str(P/'niw_rate_compat.arm64.o');x=subprocess.run(cmd,cwd=old.get('cwd',str(R)),capture_output=True,text=True);(P/'arm64-compile.log').write_text(x.stdout+x.stderr);(P/'arm64-command.json').write_text(json.dumps({'argv':cmd,'cwd':old.get('cwd',str(R)),'exit':x.returncode,'qualification':'selected normalC flags; source/outputsubstitution plus explicit private mappingheaderinclude; compile only, no native linkage'},indent=2)+'\n');x.check_returncode();print('PASS selected normal ARM64 rate compatibility helper')
