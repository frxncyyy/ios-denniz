from pathlib import Path
import re,json,hashlib,collections
P=Path(__file__).resolve().parent;lines=(P/'snapshot.log').read_text(errors='replace').splitlines()
def fields(line):
 return {k:int(v,16) if v.startswith('0x') else int(v) for k,v in re.findall(r'\b([a-zA-Z_][a-zA-Z_0-9]*)=(-?0x[0-9a-fA-F]+|-?\d+)',line)}
rows=[]
for lineno,line in enumerate(lines,1):
 m=re.search(r'NORD2 WIFI IOS ([A-Z]+):',line)
 if m:rows.append(dict(kind=m[1],line=lineno,**fields(line)))
by=collections.defaultdict(list)
for x in rows:by[x['kind']].append(x)
transactions={};request=None;admission=None;channels=[];active=None;last_control=None;output_correlations=[];issues=[]
for x in rows:
 k=x['kind']
 if k=='SCANREQ':request=x;channels=[]
 elif k=='CHANNEL':channels.append(x)
 elif k=='ADMISSION':admission=x
 elif k=='ENQUEUE':
  g=x['generation'];assert g not in transactions
  transactions[g]={'request':request,'admission':admission,'channels_explicitly_logged':channels.copy(),'enqueue':x,'results':[]};active=g
 elif k=='SCAN':transactions[x['generation']]['scan']=x
 elif k=='EVENT':
  assert active is not None;transactions[active]['event']=x
 elif k=='COMPLETE':transactions[x['generation']]['complete']=x
 elif k=='RESULT':transactions[x['generation']]['results'].append(x)
 elif k=='CONTROL':last_control=x
 elif k=='OUTPUT':output_correlations.append({'output':x,'preceding_control':last_control})
summary=[]
for g,t in sorted(transactions.items()):
 req,adm,enq,scan,event,done=t['request'],t['admission'],t['enqueue'],t['scan'],t['event'],t['complete'];res=t['results'];n=done['rows']
 assert adm['status']==enq['status']==scan['status']==done['cache_status']==0
 assert scan['ticket']==enq['ticket']==done['ticket'] and done['current']==1
 assert scan['done']==1 and all(scan[k]==0 for k in ['partial','canceled','error','rx_error'])
 assert event['event']==10 and event['published']==event['notified']==1 and event['status']==0
 assert [x['next'] for x in res if x['status']==1]==list(range(1,n+1))
 assert all(x['count']==n for x in res) and len(res)==n+1 and res[-1]['status']==0 and res[-1]['next']==n
 assert scan['bss']==n+done['omitted']
 assert req['flags_lo']==32 and req['flags_hi']==0 and req['rssi_floor']==-2147483648
 assert req['channels']==adm['requested_channels'] and adm['owner_channels']==22
 if t['channels_explicitly_logged']:
  expect=list(range(1,14)) if req['channels']==13 else [36,40,44,48,149,153,157,161,165]
  assert [c['number'] for c in t['channels_explicitly_logged']]==expect
  assert all(c['version']==1 and c['flags']==(10 if req['channels']==13 else 18) for c in t['channels_explicitly_logged'])
 summary.append({'generation':g,'ticket':enq['ticket'],'sequence':scan['sequence'],'requested_channels':req['channels'],'request_flags':req['flags_lo'],'channel_flags_or':adm['channel_flags_or'],'channel_min':adm['channel_min'],'channel_max':adm['channel_max'],'rx_frames':scan['frames'],'backend_bss':scan['bss'],'encoded_rows':n,'omitted':done['omitted'],'omit_mask':done['omit_mask'],'enqueue_ms':enq['t_ms'],'event_ms':event['t_ms'],'completion_ms':done['t_ms'],'enqueue_to_event_ms':event['t_ms']-enq['t_ms'],'request_to_event_ms':event['t_ms']-req['t_ms'],'result_positive_rows':n,'eof_rows':1,'result_progression_complete':True})
fail_output=[x for x in output_correlations if x['output']['status']!=0]
assert len(fail_output)==1 and fail_output[0]['preceding_control']['request']==207 and fail_output[0]['output']['requested']==10016
assert all(x['status']>=0 for x in by['RESULT'])
controls11=[x for x in by['CONTROL'] if x['request']==11]
assert len(controls11)==8 and [x['status'] for x in controls11]==[0]*7+[0xe0820446]
clients=by['CLIENT'];firstarmed=next(x for x in by['PIPE'] if x.get('phase')==3)
report={'status':'OBSERVED_SCAN_DELIVERY_CHAIN_PASS_BOUNDED_PREFIX','capture':json.loads((P/'capture.json').read_text()),'transactions':summary,'counts':{'scan_requests':len(by['SCANREQ']),'admissions':len(by['ADMISSION']),'enqueued':len(summary),'scan_complete':len(by['SCAN']),'events_published_notified':len(by['EVENT']),'cursor_positive_rows':sum(x['encoded_rows'] for x in summary),'cursor_eof_rows':len(summary),'backend_bss_across_scans_not_unique_APs':sum(x['backend_bss'] for x in summary),'omitted_across_scans':sum(x['omitted'] for x in summary),'directly_logged_GET11_success_controls':7,'directly_logged_GET11_eof_controls':1,'negative_result_rows':0,'GET11_OOL_failures_observed':0,'unrelated_OUTPUT_207_unsupported':1},'timings':{'first_client_ms':clients[0]['t_ms'],'first_armed_ms':firstarmed['t_ms'],'initialization_gap_ms':firstarmed['t_ms']-clients[0]['t_ms'],'all_client_root_busy':[x['root_busy'] for x in clients],'enqueue_to_event_min_ms':min(x['enqueue_to_event_ms'] for x in summary),'enqueue_to_event_max_ms':max(x['enqueue_to_event_ms'] for x in summary)},'failed_output_correlations':fail_output,'issues':issues,'limits':['Frozen local append-only file prefix; later collector output not evaluated','108 rows are repeated scan stream rows, not108 unique APs','First7 inline GET11 successes and firstEOF have explicit perrequestCONTROL; later control diagnostics throttled but all cursor sequences contiguous and terminate exactly atcount with no retries/negative rows','Kernel/MIG completion does not itself prove visual Settings display; human observation pending','No fresh firmwareownership/RX error observed in18SCAN summaries; this is not a universal hardware proof','No USB access, new build or canonical edit']}
(P/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'counts':report['counts'],'timings':report['timings'],'per_scan':summary},indent=2))
