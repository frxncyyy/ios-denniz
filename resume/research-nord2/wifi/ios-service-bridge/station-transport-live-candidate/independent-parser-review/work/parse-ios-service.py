#!/usr/bin/env python3
"""Summarize numeric scan/service evidence plus bounded connection observations."""
from pathlib import Path
import argparse,json,re,importlib.util
_spec=importlib.util.spec_from_file_location("connection_receipt",Path(__file__).with_name("parse-connection.py"))
_connection=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_connection)

def parse(text,raw_data=None):
    records={};issues=[]
    for line in text.splitlines():
        match=re.search(r'NORD2 WIFI IOS ([A-Z ]+): (.*)',line)
        if not match:continue
        kind,body=match.groups();fields={};valid=True
        for key,value in re.findall(r'([a-z_]+)=(-?(?:0x[0-9a-fA-F]+|[0-9]+))',body):
            if key in fields:issues.append('duplicate numeric field in '+kind);valid=False
            fields[key]=int(value,16 if value.startswith('0x') else 10)
        if valid:records.setdefault(kind,[]).append(fields)
    owner=any(x==dict(active=1,retained=1,boot_complete=1,ready=1) for x in records.get('OWNER',[]))
    registered=any(x.get('registered')==1 and x.get('carrier')==0 and x.get('media',0)&0xe0==0x80 for x in records.get('REGISTRY',[]))
    enqueued={x['ticket']:x for x in records.get('ENQUEUE',[]) if x.get('status')==0 and 'ticket' in x}
    scans={x['ticket']:x for x in records.get('SCAN',[]) if 'ticket' in x}
    completions=[];seen=set()
    for kind in ('ENQUEUE','SCAN'):
        tickets=[x.get('ticket') for x in records.get(kind,[]) if 'ticket' in x]
        if len(tickets)!=len(set(tickets)):issues.append('duplicate ticket in '+kind)
    for x in records.get('COMPLETE',[]):
        ticket=x.get('ticket');start=enqueued.get(ticket);scan=scans.get(ticket)
        if ticket in seen:issues.append('duplicate terminal completion');continue
        seen.add(ticket)
        coherent=bool(start and scan and start.get('generation')==scan.get('generation')==x.get('generation'))
        accepted=coherent and x.get('backend')==0 and x.get('current')==1 and x.get('cache_status')==0 and scan.get('status')==0 and scan.get('done')==1 and scan.get('partial')==0 and scan.get('canceled')==0 and scan.get('error')==0 and scan.get('rx_error')==0
        completions.append(dict(ticket=ticket,coherent=coherent,accepted=bool(accepted),rows=x.get('rows'),omitted=x.get('omitted'),omit_mask=x.get('omit_mask'),completion_ms=x.get('t_ms'),elapsed_ms=(x['t_ms']-start['t_ms']) if start and 't_ms' in start and 't_ms' in x else None))
    if issues:
        for x in completions:x['accepted']=False
    unsupported=sorted({x['request'] for kind in ('CONTROL','REQUEST') for x in records.get(kind,[]) if x.get('unsupported')==1 and 'request' in x})
    output_failures=sum(x.get('status',0)!=0 for x in records.get('OUTPUT',[]))
    return dict(owner_ready=owner,interface_registered_carrier_down=registered,
        network_class_registered=any(x.get('registered')==1 and x.get('network_class')==1 for x in records.get('REGISTRY',[])),
        event_pipe_armed=any(x.get('selector')==2 and x.get('phase')==3 and x.get('status')==0 for x in records.get('PIPE',[])),
        event_memory_types_mapped=sorted({x['type'] for x in records.get('MAP',[]) if x.get('status')==0 and 'type' in x}),
        scan_request_profiles=records.get('SCANREQ',[]),
        channel_profiles=records.get('CHANNEL',[]),
        client_observations=records.get('CLIENT',[]),
        iokit_observations=[line for line in text.splitlines() if line.startswith('NORD2 IOKIT:')],
        pipe_observations=records.get('PIPE',[]),
        event_observations=records.get('EVENT',[]),
        output_errors=output_failures,
        unsupported_output_responses=sum(x.get('status')==0xe00002c7 for x in records.get('OUTPUT',[])),
        admission_results=records.get('ADMISSION',[]),
        client_opens=sum(x.get('opened')==1 for x in records.get('CLIENT',[])),
        scans_enqueued=len(enqueued),scan_completions=completions,
        successful_fresh_scan_completions=sum(x['accepted'] for x in completions),
        notified_scan_events=sum(x.get('event')==10 and x.get('published')==1 and x.get('notified')==1 for x in records.get('EVENT',[])),
        result_rows_delivered=None if output_failures and any(x.get('status',-1)>0 for x in records.get('RESULT',[])) else sum(x.get('status',-1)>0 for x in records.get('RESULT',[])),
        result_rows_encoded=sum(x.get('status',-1)>0 for x in records.get('RESULT',[])),
        result_exhaustion_observed=any(x.get('status')==0 for x in records.get('RESULT',[])),
        unsupported_request_ids=unsupported,issues=issues,
        scope='Numeric driver observations only; OUTPUT errors may be dispatch refusals rather than copy failures; delivered-row count is conservative when encoded rows and errors coexist. Settings visibility requires human observation.',
        connection_observations=_connection.parse(text.encode("utf8") if raw_data is None else raw_data))

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);args=ap.parse_args()
    raw=args.log.read_bytes();print(json.dumps(parse(raw.decode('utf8',errors='replace'),raw),indent=2))
