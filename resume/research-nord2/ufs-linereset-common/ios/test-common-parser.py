#!/usr/bin/env python3
from pathlib import Path
import hashlib,importlib.util
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('common',HERE/'parse-common.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
cold='NORD2 WIFI COMMON COLD: status=0 valid=0x7 cpu=0 top=0 power=0\n'
result='NORD2 WIFI COMMON: status=0 phase=4 snapshots=40 verified=40 error=0 restore_error=0 clean=1\n'
restore='NORD2 WIFI COMMON RESTORE: touched=0xffffffffff restored=0xffffffffff valid=0x3fff field=14\n'
time='NORD2 WIFI COMMON TIME: ticks=130000 frequency=13000000\n'
outer='NORD2 WIFI: power probe stage=3 id=0x20010101 cfg=0x20010101 success=1\nNORD2 WIFI: power probe restored=1\n'
success=cold+result+restore+time+outer
parse=lambda s:m.parse(s.encode())
r=parse(success);assert r['complete'] and r['success'] and r['elapsed_seconds']==.01,r['issues']
assert r['raw_text']==success and r['source_sha256']==hashlib.sha256(success.encode()).hexdigest()
assert parse(success.replace('\n','\r\n'))['success']
# Actual opt-out path emits zero common lines; cannot prove why from silence.
assert parse(outer)['classification']=='common_not_observed' and not parse(outer)['success']
assert parse('unrelated log\n')['classification']=='common_not_observed'
for reason in ('owner-or-AP','rgu-map','table-map'):
 raw=(cold if reason=='table-map' else '')+f'NORD2 WIFI COMMON: skipped=1 reason={reason}\n'+outer
 r=parse(raw);assert r['complete'] and r['classification']=='skipped_guard',r['issues']
for skip in ['valid=0 field=14 raw=0','valid=0x7 field=2 raw=4','valid=0x3 field=2 raw=0']:
 r=parse(cold+'NORD2 WIFI COMMON: skipped=1 '+skip+'\n'+outer);assert r['complete'] and not r['success'],r['issues']
for raw in [cold.replace('status=0','status=1').replace('cpu=0','cpu=1')+outer,cold.replace('status=0 valid=0x7','status=-1 valid=0x1')+outer]:
 r=parse(raw);assert r['complete'] and r['classification']=='cold_guard_refused',r['issues']
failed=result.replace('status=0 phase=4 snapshots=40 verified=40 error=0','status=-1 phase=3 snapshots=40 verified=2 error=-4')
partial_restore=restore.replace('0xffffffffff','0x7')
r=parse(cold+failed+partial_restore+time+outer);assert r['complete'] and r['classification']=='clean_failed_transaction',r['issues']
snapshot_fail=result.replace('status=0 phase=4 snapshots=40 verified=40 error=0','status=-1 phase=1 snapshots=3 verified=0 error=-2')
r=parse(cold+snapshot_fail+restore.replace('0xffffffffff','0')+time+outer);assert r['complete'] and not r['success'],r['issues']
dirty_result=failed.replace('restore_error=0 clean=1','restore_error=-6 clean=0')
dirty_restore=partial_restore.replace('restored=0x7','restored=0x3')
dirty=cold+dirty_result+dirty_restore+time+m.DIRTY+'\n'
r=parse(dirty);assert r['complete'] and r['classification']=='dirty_halt' and not r['success'],r['issues']
r=parse(dirty.rsplit(m.DIRTY,1)[0]);assert r['complete'] and r['classification']=='dirty_state_halt_not_observed' and not r['success']
lines=success.splitlines(keepends=True)
for i in range(len(lines)):
 for bad in [''.join(lines[:i]+lines[i+1:]),''.join(lines[:i]+[lines[i]]+lines[i:]),''.join(lines[:i]+[lines[i][:20]+'\n']+lines[i+1:])]:
  assert not parse(bad)['success'],(i,bad)
for bad in [success[:-1],success[:-2],success.replace('frequency=13000000','frequency=0'),success.replace('snapshots=40','snapshots=39'),success.replace('verified=40','verified=39'),success.replace('touched=0xffffffffff','touched=0x7'),success.replace('restored=0xffffffffff','restored=0xfffffffffe'),success.replace('cpu=0','cpu=1'),success.replace('valid=0x7','valid=0x3'),success.replace('restored=1','restored=0'),success.replace('id=0x20010101','id=0'),success.replace('frequency=13000000','frequency=18446744073709551616'),success+ m.DIRTY+'\n',dirty+outer,cold+'NORD2 WIFI COMMON: skipped=1 reason=owner-or-AP\n'+outer,success.replace('valid=0x3fff','valid=0x2'),success.replace('NORD2 WIFI COMMON TIME:','NORD2 WIFI COMMON TIME: NORD2 WIFI COMMON:')]:
 assert not parse(bad)['complete'],bad
# Exact canonical formats source checks prevent fixture drift.
ROOT=HERE.parents[3];adapter=(ROOT/'shim/nord2-xnu/wifi_common_adapter.inc').read_text()
for label in (' COLD:',': status=',' RESTORE:',' TIME:',': skipped=1 reason=',': skipped=1 valid='):
 assert m.PREFIX+label in adapter
print('PASS commonparser:40snapshot+verify+mask+outer/cold success; skips/optout/cleanfailure/dirtyhalt shapes; missing/duplicate/truncation/conflict/overflow; raw/SHA; noGUI/FW/RF inference')
