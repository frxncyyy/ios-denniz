from pathlib import Path
import subprocess,uuid,json,datetime,sys
out=Path(__file__).resolve().parent
root=out.parents[4]
sid='b4585316-9654-4897-b2d0-905116396d3f'
argv=['$HOME/.local/bin/claude','--print','--model','opus','--effort','high','--name','Nord2 wifid actual crash diagnosis','--resume',sid,'--output-format','stream-json','--verbose','--safe-mode','--restricted','--permission-mode','dontAsk','--permission-prompts','none','--tools','Read,Grep,Glob','--allowedTools','Read,Grep,Glob','--strict-mcp-config','--mcp-config','{"mcpServers":{}}','--add-dir',str(root.parent/'nord2-current-kernel')]
meta=dict(session_id=sid,requested_model='opus',effort='high',started=datetime.datetime.now().astimezone().isoformat(),scope='Saved-source and hardware-log diagnosis; no device or canonical mutations',argv=argv,cwd=str(root),status='starting')
with (out/'stream.jsonl').open('xb') as stream,(out/'stderr.log').open('xb') as err,(out/'PROMPT.md').open('rb') as prompt:
 proc=subprocess.Popen(argv,cwd=root,stdin=prompt,stdout=stream,stderr=err)
 meta.update(pid=proc.pid,status='running');(out/'session.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps({'pid':proc.pid,'session_id':sid,'started':True}),flush=True)
 code=proc.wait()
meta.update(exit_code=code,finished=datetime.datetime.now().astimezone().isoformat(),status='finished' if code==0 else 'failed')
(out/'session.json').write_text(json.dumps(meta,indent=2)+'\n')
texts=[];result=None
for line in (out/'stream.jsonl').read_text(errors='replace').splitlines():
 try: d=json.loads(line)
 except ValueError: continue
 if d.get('type')=='assistant':
  texts.extend(c['text'] for c in d.get('message',{}).get('content',[]) if c.get('type')=='text')
 if d.get('type')=='result':result=d
(out/'assistant-text.md').write_text('\n\n'.join(texts)+'\n')
if result is not None:
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 (out/'REPORT.md').write_text(result.get('result','')+'\n')
print(json.dumps({'exit_code':code,'result_subtype':result.get('subtype') if result else None}),flush=True)
sys.exit(code)
