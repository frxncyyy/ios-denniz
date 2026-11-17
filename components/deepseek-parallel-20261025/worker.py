#!/usr/bin/env python3
import datetime,json,os,pathlib,subprocess,sys,time
R=pathlib.Path(__file__).resolve().parent;W=R.parents[2];role=sys.argv[1];info=json.loads((R/"roles.json").read_text())[role]
env=os.environ.copy()
for key in ["DISPLAY","WAYLAND_DISPLAY","DBUS_SESSION_BUS_ADDRESS","HYPRLAND_INSTANCE_SIGNATURE","SSH_AUTH_SOCK"]:env.pop(key,None)
env.update({"OPENCODE_CONFIG":str(R/"config/opencode.json"),"CARGO_BUILD_JOBS":"1","MAKEFLAGS":"-j1","CMAKE_BUILD_PARALLEL_LEVEL":"1","RAYON_NUM_THREADS":"1","OMP_NUM_THREADS":"1","OPENBLAS_NUM_THREADS":"1","MKL_NUM_THREADS":"1","PYTHONUNBUFFERED":"1","GIT_TERMINAL_PROMPT":"0","CARGO_TARGET_DIR":str(R/"workers"/role/"cargo-target")})
args=["/usr/bin/bwrap","--ro-bind","/","/","--dev","/dev","--proc","/proc","--tmpfs","/tmp","--tmpfs","/run","--unshare-pid","--unshare-ipc","--unshare-uts","--die-with-parent","--new-session","--bind",str(W),str(W)]
for path in ["$HOME/.local/share/opencode","$HOME/.cache/opencode","$HOME/.local/state/opencode"]:
 if pathlib.Path(path).is_dir():args += ["--bind",path,path]
if (W/".git").exists():args += ["--ro-bind",str(W/".git"),str(W/".git")]
args += ["--ro-bind",str(R/"config"),"$HOME/.config/opencode","--chdir",str(W),"--","/usr/bin/opencode","run","--standalone","--auto","--format","json","--thinking","--model","opencode-go/deepseek-v4.1-flash#max","--session",info["session"],"--title",info["title"],"--file",str(R/"HANDOFF.md"),"--file",str(R/"workers"/role/"TASK.md"),"Implement your assigned Metal production work now. Read both handoffs and existing board messages before editing. Coordinate with other DeepSeek and Claude-native agents through the shared board; send an initial plan/ABI proposal before shared interface decisions. Continue until your coherent implementation and real host tests are ready for review, with explicit unresolved gaps. Do not access the phone or publish. Model requested: DeepSeek4.1Flash max."]
log=R/"logs"/(role+".jsonl");txt=R/"logs"/(role+".log");status=R/"workers"/role/"runtime.json"
def setstatus(**extra):status.write_text(json.dumps({"role":role,"session":info["session"],"model":"opencode-go/deepseek-v4.1-flash#max","updated":datetime.datetime.now(datetime.timezone.utc).isoformat(),**extra},indent=2))
with log.open("a",buffering=1) as raw,txt.open("a",buffering=1) as visible:
 def show(s):visible.write(s+"\n");print(s,flush=True)
 show(info["title"]+"\nModel: opencode-go/deepseek-v4.1-flash#max\nShared board: "+str(R/"board"))
 child=subprocess.Popen(args,cwd=W,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1);setstatus(state="running",pid=child.pid)
 for line in child.stdout:
  raw.write(line)
  try:
   e=json.loads(line);p=e.get("part",{});kind=e.get("type","")
   if kind in ["text","reasoning"]:show(p.get("text",e.get("text","")))
   elif kind=="tool_use":
    state=p.get("state",{});show("["+p.get("tool","tool")+"] "+state.get("status","")+" "+str(state.get("input",{}))[:1800])
    if state.get("status")=="completed":show(str(state.get("output",""))[-2500:])
   elif kind=="error":show(line.rstrip())
  except (ValueError,TypeError):show(line.rstrip())
 code=child.wait();setstatus(state="finished" if code==0 else "error",exit_code=code);show(f"Session finished exit={code}. Review STATUS.md and board; no phone operation performed by this launcher.")
