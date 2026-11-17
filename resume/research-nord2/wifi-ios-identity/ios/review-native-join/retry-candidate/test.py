from pathlib import Path
import subprocess,json,hashlib,os,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());S=R/'shim/nord2-xnu';F=R/'tools/fixtures/nord2-wifi-station';B=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl';out=P/'verification';out.mkdir(exist_ok=True)
base=['wifi_station_transaction','wifi_station_wire','wifi_wpa2','wifi_wpa_owner','wifi_wpa_crypto','wifi_normal_transport','wifi_capability','wifi_main_transport','wifi_main_command']
files=[S/(n+'.c')for n in base]+[B/'src'/x for x in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']]
results=[]
for name,source in [('baseline',S/'wifi_station_transport.c'),('candidate',P/'proposed/shim/nord2-xnu/wifi_station_transport.c')]:
 command=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-const-variable','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread','-I'+str(S),'-I'+str(F),'-I'+str(B/'inc'),'-I'+str(B/'src'),str(P/'test-retry.c'),str(source),*map(str,files),'-o',str(out/name)]
 q=subprocess.run(command,capture_output=True,text=True);(out/(name+'-build.log')).write_text(q.stdout+q.stderr);assert q.returncode==0,q.stderr
 env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1';q=subprocess.run([str(out/name)],capture_output=True,text=True,env=env);(out/(name+'.log')).write_text(q.stdout+q.stderr)
 assert q.returncode==0 if name=='candidate' else q.returncode!=0 and 'peer handoff refused Retry=1' in q.stderr,(name,q.stderr)
 results.append({'name':name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'exit':q.returncode,'output':q.stdout.strip(),'expected':name=='candidate','command':command})
report={'status':'PASS_CANDIDATE_AND_BASELINE_NEGATIVE_CONTROL','results':results,'synthetic_firmware_only':True,'hardware_claim':False,'canonical_changes':False}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='results'}));print(results[1]['output'])
