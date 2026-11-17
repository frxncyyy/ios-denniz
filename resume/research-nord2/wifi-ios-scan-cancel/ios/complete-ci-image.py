from pathlib import Path
import subprocess,json,sys
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists());s=json.loads((P/'stage.json').read_text());run=str(s['ci_run']);source=s['source_commit'];C=R/('research/ci-'+run)
with (P/'ci-watch.log').open('x') as log:subprocess.run(['gh','run','watch',run,'--repo','<github-user>/ios-sweet','--interval','30','--exit-status'],cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
r=json.loads(subprocess.check_output(['gh','run','view',run,'--repo','<github-user>/ios-sweet','--json','databaseId,headSha,status,conclusion,attempt,url'],cwd=R,text=True));assert r['headSha']==source and r['conclusion']=='success';C.mkdir(exist_ok=False);(C/'run.json').write_text(json.dumps(r,indent=2)+'\n');print('CI succeeded on exact source',source,flush=True)
with (C/'build.log').open('x') as log:subprocess.run(['gh','run','view',run,'--repo','<github-user>/ios-sweet','--log'],cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run(['gh','run','download',run,'--repo','<github-user>/ios-sweet','--name','xnu-kernel','--name','xnu-buildtree','--dir',str(C)],cwd=R,check=True)
subprocess.run([sys.executable,str(P/'verify-ci.py'),'--ci-run',run,'--source',source],cwd=R,check=True)
subprocess.run([sys.executable,str(P/'build-image.py'),'--ci-run',run,'--source',source],cwd=R,check=True)
b=json.loads((P/'build-result.json').read_text());O=P/'independent-image-review/final'
subprocess.run([sys.executable,str(P/'verify-image.py'),'--check-existing','--source',source,'--ci-run',run,'--attempt',str(r['attempt']),'--kernel-sha256',b['kernel_sha256'],'--image-sha256',b['image_sha256'],'--image-bytes',str(b['image_bytes']),'--payload-sha256',b['payload_sha256'],'--output',str(O)],cwd=R,check=True)
print('Image build and separate offline verification complete; NOT authorized or flashed',flush=True)
