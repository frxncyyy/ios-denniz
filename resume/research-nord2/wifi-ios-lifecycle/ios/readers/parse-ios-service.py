#!/usr/bin/env python3
"""Summarize numeric scan/service evidence plus bounded connection observations."""
from pathlib import Path
import argparse,json,re,importlib.util
_spec=importlib.util.spec_from_file_location("connection_receipt",Path(__file__).with_name("parse-connection.py"))
_connection=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_connection)

SCHEMAS={
 'ENQUEUE':'status generation ticket t_ms',
 'SCAN':'ticket generation status sequence frames bss done partial canceled error rx_error',
 'COMPLETE':'generation ticket backend current cache_status rows omitted omit_mask t_ms',
 'SCANHISTORY':'generation source_rows display_rows retained_rows overflow dropped ttl_ms',
 'RESULT':'status next count generation',
}
LIMIT=8*1024*1024

def parse(text,raw_data=None):
    records={};issues=[];ordered=[]
    if len(text.encode('utf8'))>LIMIT:return dict(issues=['input>8MiB'],scan_completions=[],successful_fresh_scan_completions=0)
    for line in text.splitlines(keepends=True):
        match=re.search(r'NORD2 WIFI IOS ([A-Z ]+): (.*)',line.rstrip('\r\n'))
        if not match:continue
        kind,body=match.groups();fields={};valid=True
        if kind in SCHEMAS:
            tokens=body.split(' ');names=SCHEMAS[kind].split()
            if not line.endswith('\n') or len(tokens)!=len(names) or len(body)>511:valid=False
            for token,name in zip(tokens,names):
                m=re.fullmatch(re.escape(name)+r'=(-?(?:0x[0-9a-fA-F]+|[0-9]+))',token)
                if not m:valid=False;continue
                v=m[1];n=int(v,16 if v.startswith('0x') else 10)
                bits=64 if name in ('generation','ticket','t_ms') else 32
                if not (-(1<<31)<=n<(1<<31) if name=='status' else 0<=n<(1<<bits)):valid=False
                fields[name]=n
            if valid:
                for key in ('bss','rows','source_rows','display_rows','retained_rows','count','next'):
                    if key in fields and fields[key]>64:valid=False
                for key in ('done','partial','canceled','current','overflow'):
                    if key in fields and fields[key] not in (0,1):valid=False
                if kind=='SCANHISTORY' and (fields['ttl_ms']!=30000 or fields['display_rows']>fields['retained_rows'] or not fields['generation']):valid=False
                if kind=='SCAN' and (fields['status'] not in range(5) or not 1<=fields['sequence']<=255 or fields['bss']>fields['frames'] or ((fields['bss']==0)!=(fields['frames']==0))):valid=False
                if kind in ('SCAN','COMPLETE','SCANHISTORY','RESULT') and not fields['generation']:valid=False
                if kind in ('SCAN','COMPLETE') and not fields['ticket']:valid=False
                if kind=='RESULT' and (fields['status'] not in (-1,0,1) or fields['next']>fields['count']):valid=False
            if not valid:issues.append('malformed/bounds '+kind);continue
        else:
            for key,value in re.findall(r'([a-z_]+)=(-?(?:0x[0-9a-fA-F]+|[0-9]+))',body):
                if key in fields:issues.append('duplicate numeric field in '+kind);valid=False
                fields[key]=int(value,16 if value.startswith('0x') else 10)
        if valid:
            records.setdefault(kind,[]).append(fields);ordered.append((kind,fields))
            if len(ordered)>8192:issues.append('numeric row budget');break
    owner=any(x==dict(active=1,retained=1,boot_complete=1,ready=1) for x in records.get('OWNER',[]))
    registered=any(x.get('registered')==1 and x.get('carrier')==0 and x.get('media',0)&0xe0==0x80 for x in records.get('REGISTRY',[]))
    enqueued={x['ticket']:x for x in records.get('ENQUEUE',[]) if x.get('status')==0 and 'ticket' in x}
    scans={x['ticket']:x for x in records.get('SCAN',[]) if 'ticket' in x}
    completions=[];seen=set();histories={};pending_scan=None;pending_history=None
    for kind,row in ordered:
        if kind=='SCAN':
            if pending_history is not None:issues.append('interleaved SCANHISTORY')
            pending_scan=row;pending_history=None
        elif kind=='SCANHISTORY':
            if pending_scan is None or pending_history is not None or row['generation']!=pending_scan['generation']:
                issues.append('orphan/duplicate/wrong-identity SCANHISTORY')
            else:pending_history=row
        elif kind=='COMPLETE':
            if pending_history is not None:
                if pending_scan['ticket']!=row['ticket'] or pending_scan['generation']!=row['generation']:issues.append('SCANHISTORY completion identity')
                else:histories[row['ticket']]=pending_history
            pending_scan=None;pending_history=None
    if pending_history is not None:issues.append('missing SCANHISTORY completion')
    for kind in ('ENQUEUE','SCAN'):
        tickets=[x.get('ticket') for x in records.get(kind,[]) if 'ticket' in x]
        if len(tickets)!=len(set(tickets)):issues.append('duplicate ticket in '+kind)
    for x in records.get('COMPLETE',[]):
        ticket=x.get('ticket');start=enqueued.get(ticket);scan=scans.get(ticket);history=histories.get(ticket)
        if ticket in seen:issues.append('duplicate terminal completion');continue
        seen.add(ticket)
        coherent=bool(start and scan and start.get('generation')==scan.get('generation')==x.get('generation'))
        clean=bool(scan and scan['status']==0 and scan['partial']==0 and history and history['overflow']==history['dropped']==0)
        overflow=bool(scan and scan['status']==1 and scan['partial']==1 and scan['bss']==64 and history and history['overflow']==1 and 0<history['dropped']<=scan['frames']-scan['bss'])
        shape=bool(history and scan and history['source_rows']==scan['bss'] and x['rows']==history['display_rows'])
        accepted=coherent and shape and x['backend']==scan['status'] and x['current']==1 and x['cache_status']==0 and scan['done']==1 and scan['canceled']==scan['error']==scan['rx_error']==0 and (clean or overflow)
        if x['current']==1 and x['cache_status']==0 and not accepted:issues.append('claimed cache completion lacks exact source/history proof')
        completions.append(dict(ticket=ticket,coherent=coherent,accepted=bool(accepted),source_overflow=overflow,source_rows=history['source_rows'] if history else None,retained_rows=history['retained_rows'] if history else None,rows=x.get('rows'),omitted=x.get('omitted'),omit_mask=x.get('omit_mask'),completion_ms=x.get('t_ms'),elapsed_ms=(x['t_ms']-start['t_ms']) if start and 't_ms' in start else None))
    if issues:
        for x in completions:x['accepted']=False
    unsupported=sorted({x['request'] for kind in ('CONTROL','REQUEST') for x in records.get(kind,[]) if x.get('unsupported')==1 and 'request' in x})
    output_failures=sum(x.get('status',0)!=0 for x in records.get('OUTPUT',[]))
    return dict(owner_ready=owner,interface_registered_carrier_down=registered,
        network_class_registered=any(x.get('registered')==1 and x.get('network_class')==1 for x in records.get('REGISTRY',[])),
        event_pipe_armed=any(x.get('selector')==2 and x.get('phase')==3 and x.get('status')==0 for x in records.get('PIPE',[])),
        event_memory_types_mapped=sorted({x['type'] for x in records.get('MAP',[]) if x.get('status')==0 and 'type' in x}),
        scan_request_profiles=records.get('SCANREQ',[]),scan_history=records.get('SCANHISTORY',[]),
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
        scope='Numeric driver observations only; OUTPUT errors may be dispatch refusals rather than copy failures; delivered-row count is conservative when encoded rows and errors coexist. Display union is bounded30s history, never a native scan union or join authority; overflow acceptance is only full64-row completed cache overflow. Native RX is not BSD or IP proof. Settings visibility requires human observation.',
        connection_observations=_connection.parse(text.encode("utf8") if raw_data is None else raw_data))

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);args=ap.parse_args()
    raw=args.log.read_bytes();print(json.dumps(parse(raw.decode('utf8',errors='replace'),raw),indent=2))
