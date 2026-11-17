#!/usr/bin/env python3
"""Synthetic report parsing only; no phone or artifact changes."""
from pathlib import Path
import importlib.util
p=Path(__file__).with_name('parse-result.py');spec=importlib.util.spec_from_file_location('x2_parser',p);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
raw='''NORD2 UFS X2 STOP: rc=0 phase=4 error=0 commands=115 sets=37 reads=3
NORD2 UFS X2 STATE: uncertain=0 rollback=1 original=1 engine_root=1 engine_handoff=1
NORD2 UFS X2 LAST: attr=0xd043 result=0 is=0x400 hcs=0x10f root_published=0 return_blocked=1
NORD2 UFS X2 SNAP A: valid=0x3ffff rx_gear=4 rx_lanes=1 rx_term=1 tx_gear=4 tx_lanes=1 tx_term=1
NORD2 UFS X2 SNAP B: hs=2 power=0x11 adapt=1 pa_timeout0=8191 pa_timeout1=65535 pa_timeout2=32767
NORD2 UFS X2 SNAP C: pa_timeout3=8191 pa_timeout4=65535 pa_timeout5=32767 local_fc0=8191 local_tc0=65535 local_afc0=32767
NORD2 UFS X2 LIMITS: valid=0x3f rx_available=2 tx_available=2 rx_connected=2 tx_connected=2 version=0x5 max_rx_gear=4
'''
def parse(s):return module.parse(s.encode())
assert parse(raw)['success'] and parse(raw)['complete']
gate=raw.replace('rc=0 phase=4 error=0 commands=115 sets=37 reads=3','rc=-1 phase=1 error=-4 commands=24 sets=0 reads=0').replace('rollback=1 original=1 engine_root=1 engine_handoff=1','rollback=0 original=0 engine_root=0 engine_handoff=0').replace('pa_timeout0=8191','pa_timeout0=123')
assert parse(gate)['classification']=='gate_refused_no_link_sets' and not parse(gate)['success']
assert parse(raw.replace('uncertain=0','uncertain=1'))['classification']=='ambiguous_or_unsafe'
lines=raw.splitlines(True)
for i in range(7):
 assert not parse(''.join(lines[:i]+lines[i+1:]))['success']
 assert not parse(''.join(lines[:i]+[lines[i][:40]+'\n']+lines[i+1:]))['success']
assert not parse(raw+raw)['success']
assert not parse(raw.replace('result=0','result=9'))['success']
assert not parse(raw.replace('valid=0x3ffff','valid=0x1ffff'))['success']
assert not parse(raw.replace('sets=37','sets=0'))['success']
assert not parse(raw.replace('root_published=0','root_published=1'))['success']
assert not parse(raw.replace('reads=3','reads=3 FRAMEBUFFER noise'))['success']
# Even complete-looking final fields are unknown without a line terminator.
assert not parse(raw[:-1])['success']
assert not parse(raw[:-1])['complete']
# Clipping true value40 to4 plus losing newline must never manufacture success.
clipped=raw.replace('max_rx_gear=4\n','max_rx_gear=40\n')[:-2]
assert clipped.endswith('max_rx_gear=4')
assert not parse(clipped)['success'] and not parse(clipped)['complete']
assert parse(clipped)['raw_evidence'][-1]['line_terminated'] is False
assert parse(clipped)['raw_evidence'][-1]['raw'].endswith('max_rx_gear=4')
assert parse(raw.replace('\n','\r\n'))['success']
assert not parse('')['success']
assert parse(''.join('[123] '+line for line in lines))['success']
print('PASS: offline7-line parser; exact success/gate/ambiguous/missing/truncated/duplicate/interleaved/masks/inconsistent counts/raw evidence')
