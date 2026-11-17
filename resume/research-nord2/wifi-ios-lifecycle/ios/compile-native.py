from pathlib import Path
import hashlib,json,subprocess
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists());P=Path(__file__).resolve().parent;O=P/'focused';O.mkdir(exist_ok=False);B=R/'research/ci-39158169212/buildtree/xnu'
old=json.loads((R/'research/nord2/wifi-vcn33/ios/focused/commands.json').read_text());c=next(x for x in old if x['name']=='sweet_touch')
cpp=json.loads((R/'research/nord2/wifi/ios-service-bridge/native-lifecycle-shell-v2/arm64-command.json').read_text())
records=[]
changed=subprocess.check_output(['git','diff','--name-only','HEAD','--','shim/nord2-xnu'],cwd=R,text=True).splitlines()
names={Path(n).name for n in changed if Path(n).suffix in ('.c','.cpp')}
names.update(('sweet_touch.c','wifi_ios_client.cpp'))
files=[B/('iokit/Kernel/arm' if n.endswith('.cpp') else 'bsd/dev')/n for n in sorted(names)]
for source in files:
    iscpp=source.suffix=='.cpp';cmd=[x for x in (cpp['argv'] if iscpp else c['argv']) if x!='-I'+str(R/'shim/nord2-xnu')];name=source.stem
    if iscpp and not any(x.startswith('-mcpu=') for x in cmd):cmd.insert(1,'-mcpu=generic') # exact current SWEET MakeInc.def
    index=next(i for i,x in enumerate(cmd) if x.endswith('LifecycleShell.cpp')) if iscpp else cmd.index('-c')+1
    cmd[index]=str(source);cmd[cmd.index('-o')+1]=str(O/(name+'.o'))
    assert '-mcpu=generic' in cmd and '-march=armv8.2-a+crypto' in cmd
    cwd=R if iscpp else Path(c['cwd'])
    with (O/(name+'.log')).open('xb') as log:run=subprocess.run(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT)
    records.append(dict(name=name,argv=cmd,cwd=str(cwd),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),exit=run.returncode))
    (O/'commands.json').write_text(json.dumps(records,indent=2)+'\n');print(name,run.returncode,flush=True)
    if run.returncode:
        print('\n'.join(line for line in (O/(name+'.log')).read_text().splitlines() if 'error:' in line));raise SystemExit(run.returncode)
print('PASS',len(files),'actual generated native units; no private include-path injection')
