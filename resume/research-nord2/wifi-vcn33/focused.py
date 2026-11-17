from pathlib import Path
import json,subprocess
R=Path(__file__).resolve().parents[3]
P=Path(__file__).resolve().parent/'ios/focused';P.mkdir(exist_ok=True)
old=json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text())
commands=[]
for name,template in [('wifi_vcn33',old[0]),('sweet_touch',old[-1])]:
 argv=list(template['argv']);argv[argv.index('-c')+1]=str(R/'research/ci-39158169212/buildtree/xnu/bsd/dev'/(name+'.c'));argv[argv.index('-o')+1]=str(P/(name+'.o'))
 with (P/(name+'.log')).open('w') as log:ret=subprocess.run(argv,cwd=template['cwd'],stdout=log,stderr=subprocess.STDOUT).returncode
 commands.append(dict(name=name,argv=argv,cwd=template['cwd'],returncode=ret))
 (P/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 print(name,ret,flush=True)
 if ret:raise SystemExit(ret)
