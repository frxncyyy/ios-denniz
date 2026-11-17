from pathlib import Path
import subprocess,json,sys
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').is_file());s=json.loads((P/'stage.json').read_text());run=str(s['ci_run']);C=R/('research/ci-'+run)
with(P/'ci-watch.log').open('x')as f:subprocess.run(['gh','run','watch',run,'-R','<github-user>/ios-sweet','--interval','45','--exit-status'],cwd=R,stdout=f,stderr=subprocess.STDOUT,check=True)
r=json.loads(subprocess.check_output(['gh','run','view',run,'-R','<github-user>/ios-sweet','--json','databaseId,headSha,status,conclusion,attempt,url'],cwd=R,text=True));assert r['conclusion']=='success'and r['headSha']==s['source_commit'];C.mkdir(exist_ok=False);(C/'run.json').write_text(json.dumps(r,indent=2)+'\n')
print('CI succeeded on exact source',r['headSha'],flush=True)
with(C/'build.log').open('x')as f:subprocess.run(['gh','run','view',run,'-R','<github-user>/ios-sweet','--log'],cwd=R,stdout=f,stderr=subprocess.STDOUT,check=True)
subprocess.run(['gh','run','download',run,'-R','<github-user>/ios-sweet','--name','xnu-kernel','--name','xnu-buildtree','--name','nord2-audio-hal','--dir',str(C)],cwd=R,check=True)
subprocess.run([sys.executable,str(P/'verify-ci.py'),'--ci-run',run,'--source',s['source_commit']],cwd=R,check=True)
subprocess.run([sys.executable,str(P/'build-image.py')],cwd=R,check=True)
print('Two images built and generic-checked; independent review remains before authorization',flush=True)
