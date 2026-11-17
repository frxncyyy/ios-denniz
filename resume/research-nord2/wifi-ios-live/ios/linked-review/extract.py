#!/usr/bin/env python3
"""Bounded live/rejoin/PID linked extraction. No automatic semantic PASS.

Default mode prepares exact committed-source inventory only. Artifact mode needs
root-notified existing kernel identity; never polls/downloads/builds or changes
a stage gate. Evidence is LLVM disassembly, not runtime or REA engine output.
"""
from pathlib import Path
import argparse,hashlib,json,re,runpy,subprocess
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').is_file())
SOURCE='c35ce62d7368a8b3f0a8c91bd52d743cc08d56a3';CI=39924876139
HELPER=R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'
SOURCES=['shim/nord2-xnu/'+n for n in ('wifi_live_control.c','wifi_live_control.h','wifi_live_control_bind.inc','wifi_station_rejoin.h','wifi_station_service_bind.inc','wifi_station_transport_bind.inc','wifi_scan_return_bind.inc','wifi_scan_service_bind.inc','wifi_normal_backend_bind.inc','wifi_station_transport.c','wifi_station_transport.h','wifi_station_data_diag.c','wifi_station_data_diag.h','wifi_capability_bind.inc','wifi_normal_transport_bind.inc','wifi_main_wfdma_bind.inc','wifi_normal_backend_forward.inc')]+['tools/nord2-mkpatch.sh','xnu/patches/0082-nord2-early-probe.patch']
# Static helpers may be inlined. Their actual linked callers must then be
# extracted; neither a missing body nor a marker is a semantic PASS.
FUNCTIONS=[
 ('usb_adapter','_nwl_usb',[]),('mailbox_submit','_nwl_submit',[]),('mailbox_take','_nwl_take',[]),('mailbox_complete','_nwl_complete',[]),('mailbox_caps','_nwl_caps',[]),('mailbox_reply','_nwl_reply',[]),('mailbox_init','_nwl_initialize',[]),
 ('USB_decoder','_nusb_reply',['_nord2_usb_enumerate']),
 ('owner_loop','_nss_owner_loop',['_ncd_worker']),('live_start','_nwl_native_start',['_nss_owner_loop','_ncd_worker']),('live_work','_nwl_native_work',['_nss_owner_loop','_ncd_worker']),('live_snapshot','_nwl_snapshot_locked',['_nwl_native_work','_nss_owner_loop','_ncd_worker']),
 ('join_enqueue','_nord2_wifi_join_enqueue',[]),('join_disconnect','_nord2_wifi_join_disconnect',[]),('owner_join','_njs_owner_work',['_nss_owner_loop','_ncd_worker']),('join_constructor','_njs_start_owner',['_njs_owner_work','_nss_owner_loop','_ncd_worker']),('native_constructor','_nst_native_prepare',['_njs_start_owner','_njs_owner_work','_nss_owner_loop','_ncd_worker']),('pure_constructor_abort','_nst_native_abort_prepare',['_njs_start_owner','_njs_owner_work','_nss_owner_loop','_ncd_worker']),
 ('scan_return','_nss_return_scans',['_njs_owner_work','_nss_owner_loop','_ncd_worker']),('rejoin_ready','_njs_rejoin_ready_locked',['_nord2_wifi_join_enqueue','_nwl_native_work','_nss_owner_loop','_ncd_worker']),('rejoin_fresh_scan','_njs_rejoin_refresh',['_njs_owner_work','_nss_owner_loop','_ncd_worker']),('rejoin_scan_done','_njs_rejoin_scan_done',[]),('scan_origin_seal','_nss_scan_return_origin_valid',['_nct_native_owner_window','_nwfn_guard','_nwfn_owner']),
 ('PID_reserve','_nsdd_pid_reserve',[]),('PID_attempt','_nsdd_pid_attempt',[]),('PID_publish','_nsdd_pid_publish',[]),('PID_local','_nsdd_pid_local',[]),('PID_status','_nsdd_pid_status',[]),('PID_final','_nsdd_pid_finish',[]),('capture_window','_nsdd_capture',[]),('diagnostic_event','_nsdd_event',[]),('diagnostic_summary','_nsdd_summary',[]),
 ('native_step','_nst_step',[]),('frame_validation','_frame',['_nst_management','_nst_security','_nst_data']),('normal_publication','_data_transmit',['_transmit','_nst_step'])]
SCOPES=[
 'EP0 exact vendorIN60/62 and OUT61/no data routes only to lock-free mailbox; old log requests retain normal path',
 'Mailbox nonce/default-off initialization, sequential nonwrapped tokens1..4095, exact previous idempotence, pending ownership, atomic reply with immutable command prefix',
 'Actual NSS worker initializes only nord2wifilive opt-in; queued mailbox work before join; IRQ decoder never invokes owner/driver locks',
 'Owner exact full generation, healthy/retiring guards, capture independent per-boot quota/window metadata; OFLD temporary profile is only selected guard and disconnect invokes existing asyncclose outside NSSlock',
 'Snapshot fixed public scalar fields/time milliseconds and JOIN_READY57; no addresses/credentials; readiness no prior attempt or actual sealed cleanup/fresh scan',
 'Rejoin requires no pending callback/keys/inflight/queues; captured DDONE key ledger empty, explicit fresh scan and preserved HW0/token/command/allfiveRX state; constructor pure rollback and monotonic fresh timestamp retained',
 'Native scan return preserves command counts/sequence/RX segmentation, read-only dual TX indices/DDONE seals and persistent later-continuation origin proof',
 'Boot PID ranges1..31/32..63/64..126, no reused IDs or kernelreset seam,127 ambiguous fallback; validated reservation immutable and bound only after successful actual index write',
 'Same reserved PID survives HW0 rebuild; first exact current generation/WLAN status only; stale/unknown/duplicate/closed cannot change outcome; no new TXS wait or storage release',
 'Normal/security local receipt remains DDONE+type6 token; existing fatal owner/DMA paths retain common halt/drain; no air/authentication/DHCP success manufactured']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,o):p.write_text(json.dumps(o,indent=2)+'\n')
def committed_pins():
 out={}
 for n in SOURCES:
  p=R/n;b=subprocess.check_output(['git','show',SOURCE+':'+n],cwd=R);assert p.read_bytes()==b,n;out[n]=sha(p)
 return out
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--artifact-ready',action='store_true');ap.add_argument('--ci',type=int,default=CI);ap.add_argument('--attempt',type=int);ap.add_argument('--kernel-sha');ap.add_argument('--kernel-bytes',type=int);a=ap.parse_args()
if not a.artifact_ready:
 assert a.attempt is None and a.kernel_sha is None and a.kernel_bytes is None
 assert a.ci==CI
 local=R/'research/nord2/wifi/live-control-20261023/validation/arm64-final'
 objects={n:{'path':str((local/n).relative_to(R)),'sha256':sha(local/n)} for n in ('wifi_live_control.o','sweet_touch.o')}
 save(P/'requirements.json',{'status':'PREPARED_ONLY_WAIT_ROOT_ARTIFACT_NOTICE','verdict':None,'expected_source':SOURCE,'expected_ci':CI,'kernel_sha256':None,'source_pins':committed_pins(),'MachO_helper':{'path':str(HELPER.relative_to(R)),'sha256':sha(HELPER)},'local_objects_context_only':objects,'local_compile_report':{'path':str((local/'report.json').relative_to(R)),'sha256':sha(local/'report.json')},'functions':FUNCTIONS,'scope':SCOPES,'code_bytes_limit':262144,'human_semantic_review_pending':True,'gates_changed':False,'not_repeated':['Parent complete CI source/generic/ISA audit','Unchanged WPA2/GTK cryptography','Unchanged PHY/radio/normalTX/TXP layouts','Image or hardware verification']})
 print('PREPARED only: exact source/functions/object pins; no kernel read, linked verdict or gates');raise SystemExit(0)
assert a.ci==CI and a.attempt and a.attempt>0 and re.fullmatch('[0-9a-f]{64}',a.kernel_sha or '') and a.kernel_bytes
q=json.loads((P/'requirements.json').read_text());assert q['source_pins']==committed_pins() and sha(HELPER)==q['MachO_helper']['sha256']
C=R/f'research/ci-{CI}';meta=json.loads((C/'run.json').read_text());assert (meta['databaseId'],meta['attempt'],meta['headSha'],meta['status'],meta['conclusion'])==(CI,a.attempt,SOURCE,'completed','success')
k=C/'xnu-kernel/kernel.development.sweet';assert k.stat().st_size==a.kernel_bytes and sha(k)==a.kernel_sha
assert [x.split()[0] for x in (k.parent/'SHA256SUMS').read_text().splitlines() if len(x.split())==2 and Path(x.split()[1].lstrip('*')).name==k.name]==[a.kernel_sha]
M=runpy.run_path(str(HELPER))['MachO'];m=M(k);out=P/'artifact';out.mkdir(exist_ok=False);records=[];extracted={};total=0
def hits(symbol):return {(addr,name) for addr,_,name in m.syms if m.text(addr) and name==symbol}
def body(label,symbol):
 global total
 if symbol in extracted:return extracted[symbol]
 assert len(hits(symbol))==1,(symbol,'missing/ambiguous exact body')
 start,end,sym=m.bounds(symbol,True);b=m.read(start,end-start);total+=len(b);assert total<=q['code_bytes_limit'],'bounded code extraction limit exceeded'
 asm=m.dump(start,end);(out/(label+'.bin')).write_bytes(b);(out/(label+'.s')).write_text(asm)
 r={'label':label,'symbol':sym,'start':hex(start),'end':hex(end),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'disassembly':str((out/(label+'.s')).relative_to(R)),'direct_call_lines':[line.strip() for line in asm.splitlines() if re.search(r'\bbl\s+',line)]}
 extracted[symbol]=r;return r
for label,symbol,fallbacks in FUNCTIONS:
 if len(hits(symbol))==1:records.append(body(label,symbol));continue
 assert fallbacks,(symbol,'required direct external helper missing/ambiguous')
 callers=[]
 for index,fallback in enumerate(fallbacks):
  if len(hits(fallback))==1:
   r=body(label+'_caller'+str(index),fallback);callers.append(r['symbol'])
   if label!='frame_validation':break
 assert callers,(symbol,'no bounded actual caller found; review scope remains unresolved')
 records.append({'label':label,'symbol':symbol,'status':'NO_UNIQUE_SEPARATE_BODY_REVIEW_ACTUAL_CALLERS','callers':callers,'not_a_semantic_PASS':True})
# Literal identity supports locating branches; presence alone is never a verdict.
markers=[]
for name in ('wifi_live_control_bind.inc','wifi_station_data_diag.c','wifi_scan_return_bind.inc'):
 for fmt in re.findall(r'printf\("(NORD2 WIFI (?:LIVE(?:FRAME)?|TXPUB|TXSTATUS|TXFINAL|TXNOTE|TXLIMIT|IOS SCANRETURN): [^"]*)"',(R/'shim/nord2-xnu'/name).read_text()):
  literal=fmt.replace('\\n','\n');assert '%s' not in literal and '%p' not in literal;needle=literal.encode()+b'\0';offsets=[];offset=0
  while True:
   at=m.data.find(needle,offset)
   if at<0:break
   offsets.append(hex(at));offset=at+len(needle)
  assert offsets,'required current emitter literal absent'
  markers.append({'literal':literal,'kernel_file_offsets':offsets,'sha256':hashlib.sha256(needle).hexdigest()})
assert not any('nsdd_test_boot_reset' in n or 'nsdd_test_pid'==n.lstrip('_') for _,_,n in m.syms),'host-only reset/inspection seam linked into kernel'
save(out/'extraction.json',{'status':'EXTRACTED_PENDING_BOUNDED_MANUAL_REVIEW','verdict':None,'identity':{'ci':CI,'attempt':a.attempt,'source_commit':SOURCE,'kernel_path':str(k.relative_to(R)),'kernel_bytes':a.kernel_bytes,'kernel_sha256':a.kernel_sha,'run_json_sha256':sha(C/'run.json')},'code_bytes':total,'distinct_bodies':len(extracted),'functions':records,'numeric_literals':markers,'source_pins':q['source_pins'],'requirements_sha256':sha(P/'requirements.json'),'MachO_helper_sha256':sha(HELPER),'human_semantic_linked_review_pending':True,'gates_changed':False,'hardware_proven':False,'DHCP_lease_proven':False})
print('EXTRACTED',len(extracted),'bounded bodies;',total,'bytes; manual verdict still pending')
