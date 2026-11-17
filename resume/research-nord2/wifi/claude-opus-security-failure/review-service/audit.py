#!/usr/bin/env python3
"""Bounded immutable trace and current lifecycle-source audit; no target execution."""
from pathlib import Path
import collections
import hashlib
import json
import re

E=Path(__file__).resolve().parent
R=next(p for p in E.parents if (p/'AGENTS.md').exists())
D=E.parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(n,x):
    with (E/n).open('x') as f:f.write(json.dumps(x,indent=2)+'\n')
log=D/'attempt-final.log'
assert log.stat().st_size==434903
assert sha(log)=='3624e56ea8b63bbc7a7684824fde28227218b4b753e6a7e56104adb6103f0226'
inputs=[log,D/'REPORT.md',R/'shim/nord2-xnu/wifi_ios_client.cpp',
        R/'shim/nord2-xnu/wifi_ios_policy.c',R/'shim/nord2-xnu/wifi_ios_control.c',
        R/'shim/nord2-xnu/wifi_regulatory_policy.c',
        R/'research/nord2/wifi/ios-service-bridge/service-compat-resume/scan-requests/README.md']
pins={str(p.relative_to(R)):sha(p) for p in inputs}
lines=log.read_text(errors='strict').splitlines()
rows=[];opens=[];pending=None;chains=[];countries=[];initializations=[];pairs=set()
for n,line in enumerate(lines,1):
    marker=re.search(r'NORD2 WIFI IOS (CLIENT|PIPE|CARD|CONTROL|JOINREQ|JOINCONTROL|JOINNOTICE|STATION): (.*)',line)
    if not marker:continue
    kind,body=marker.groups()
    fields={k:int(v,0) for k,v in re.findall(r'([a-zA-Z_][a-zA-Z_0-9]*)=(0x[0-9a-f]+|[0-9]+)',body)}
    row=dict(line=n,kind=kind,fields=fields);rows.append(row)
    if kind=='CLIENT':opens.append(row)
    if kind=='PIPE' and fields.get('selector')==1:
        pending=dict(pipe1=row,controls=[],client_assignment='Not uniquely attributable: no client/task ID in rows.')
    if kind=='CONTROL':
        pairs.add((fields['request'],fields['operation']))
        if pending:pending['controls'].append(row)
        if fields['request']==51:countries.append(row)
        if fields['request']==106:
            initializations.append(dict(control=row,preceding_open=opens[-1] if opens else None,
                                        assignment='Nearest preceding open, not proved object identity.'))
    if kind=='PIPE' and fields.get('selector')==5 and pending:
        if any(c['fields']['request']==97 for c in pending['controls']):
            pending['pipe5']=row
            pending['enclosing_setup_ms']=fields['t_ms']-pending['pipe1']['fields']['t_ms']
            chains.append(pending)
        pending=None
counts=collections.Counter(x['kind'] for x in rows)
assert counts['JOINREQ']==counts['JOINCONTROL']==counts['JOINNOTICE']==counts['STATION']==0
card=[r for r in rows if r['kind']=='CARD']
assert card and all(r['fields']==dict(bytes=13,features=2,byte0=8,byte1=64) for r in card)
assert all('byte0=0x8 byte1=0x40' in lines[r['line']-1] for r in card)
assert len(countries)==5 and all(r['fields']['status']==0xe00002c7 and r['fields']['bytes']==3 for r in countries)
source=(R/'shim/nord2-xnu/wifi_ios_client.cpp').read_text()
assert source.index('if(phase!=Armed||!ownsReference)return kIOReturnNotReady;',source.index('IOReturn NiwlLifecycleClient::joinLocked'))<source.index('NORD2 WIFI IOS JOINREQ:')
assert 'IOReturn NiwlLifecycleClient::clientClose(void){closeOwned();terminate();return kIOReturnSuccess;}' in source
assert 'IOReturn NiwlLifecycleClient::clientDied(void){closeOwned();terminate();return kIOReturnSuccess;}' in source
assert 'request==51' not in source
evidence=dict(input_pins=pins,trace_bytes=log.stat().st_size,counts=dict(counts),
              client_opens=opens,get97_enclosing_setup_chains=chains,
              initialization_signature_get106=initializations,get51=countries,
              observed_control_request_operation_pairs=sorted(pairs),
              global_observed_pairs_including_card_and_power_trace_slots=len(pairs|{(12,0),(19,0)}),
              no_per_client_identity=True,no_control_call_timestamp=True,
              lifecycle_death_or_close_not_logged=True)
save('trace-evidence.json',evidence)
report=dict(verdict='BOUNDED_REVIEW_COMPLETE_NO_CAUSAL_KERNEL_FIX_PROVEN',findings=[
 dict(id='F1',status='OBSERVED_CONTRACT_GAP_CAUSALITY_UNKNOWN',
      summary='GET51/3-byte country is unsupported five times after network-selection period; source has no GET51 branch.',
      evidence='All five CONTROL rows return 0xe00002c7. First at lines4490–4491 is followed by GET27 success at4492 and OUTPUT success4493.',
      limitation='Does not show caller failure handling or prove country refusal prevents SET20. World00 is native selected fallback when SKU exists and efuse is invalid, not a proved iOS join acceptance rule.'),
 dict(id='F2',status='REPEATED_INITIALIZATION_NOT_CRASH_PROOF',
      summary='Seven GET106 initialization signatures and repeated successful client opens occur; later groups recur around15.5s.',
      evidence='get106 nearest-open timestamps: '+','.join(str(x['preceding_open']['fields']['t_ms']) for x in initializations),
      limitation='No PID/task/client ID, exit reason, clientDied/clientClose/stop timestamp, or process exit proof. New opens prove new kernel client allocations only; request groups cannot be uniquely assigned to objects.'),
 dict(id='F3',status='DELAY_BRACKET_ONLY',
      summary='First GET96/97 setup chain takes3ms from PIPE1 to PIPE5; six later chains takeabout5.1s.',
      evidence='Measured enclosing milliseconds: '+','.join(str(x['enclosing_setup_ms']) for x in chains),
      limitation='CONTROL97 has no timestamp and no call entry/exit timing. Logs cannot identify the exact five-second wait owner. Current unsupported branch has no explicit sleep/wait, but elapsed runtime is not measured.'),
 dict(id='F4',status='ABSENCE_SCOPE_CORRECTION',
      summary='No recognized normal SET20/JOINCONTROL or native join receipt is present; the first recognized join would not be hidden by ordinary log budgets.',
      evidence='traceControl uses40 distinct pairs/8each; observed global union including CARD/POWER is'+str(evidence['global_observed_pairs_including_card_and_power_trace_slots'])+'. JOINCONTROL executes after recognized20/22 returns, including NotReady/NoMemory.',
      limitation='JOINREQ is after Armed/reference, existing ticket and allocation guards. Malformed transport, wrong task, closed client, OOL/shape/interface failures can reject before recognizable join logging. Thus no proof that user space never made any raw externalMethod call.'),
],source_anchors=[
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=83,meaning='Per-client counters/trace slots initialized zero.'),
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=106,meaning='40 request/op slots;8 emissions each.'),
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=110,meaning='Silent dispatch/OOL/shape preconditions.'),
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=134,meaning='Recognized SET20/22 return status logged after join/disconnect path.'),
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=233,meaning='clientClose/clientDied identical close+terminate with no distinguishing log.'),
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=257,meaning='New client instantiated, owned/attached/started then opened log; no PID/object identity.'),
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=326,meaning='GET dispatch; GET51 absent, fallback Unsupported.'),
 dict(path='shim/nord2-xnu/wifi_ios_client.cpp',line=358,meaning='join guards precede decode and JOINREQ logging.'),
 dict(path='shim/nord2-xnu/wifi_regulatory_policy.c',line=23,meaning='Native country SKU prerequisite; invalid efuse selects3030/world00.'),
],next_evidence=['Actual userland process crash/exit report or unified log naming task/wait and caller return handling.',
                 'Selected consumer GET51 error propagation, being investigated by root.',
                 'If a combined future build is otherwise justified, task/client identity and bounded entry/early-refusal/lifecycle timestamps could distinguish missing call from malformed call; no diagnostic-only build requested.'],
 no_source_fix_proposed=True,no_device_ci_or_canonical_mutation=True,
 limits=['No userland crash, exact timeout owner, country fatality, hidden malformed call or association failure cause proved.',
         'No need to revisit radio, EAPOL or native transport before a recognized join exists.',
         'No association/key/DHCP/IP proof.'])
save('report.json',report)
assert all(sha(R/p)==v for p,v in pins.items())
files=['audit.py','trace-evidence.json','report.json']
save('manifest.json',dict(files={n:sha(E/n) for n in files},input_pins=pins))
print(json.dumps(dict(report_sha256=sha(E/'report.json'),manifest_sha256=sha(E/'manifest.json'),chains=[x['enclosing_setup_ms'] for x in chains],counts=dict(counts),pairs=evidence['global_observed_pairs_including_card_and_power_trace_slots']),indent=2))
