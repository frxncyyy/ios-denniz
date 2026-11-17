#!/usr/bin/env python3
import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('parser', Path(__file__).with_name('parse-result.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
# Keep the parser schema tied to the isolated C report, not a guessed fixture.
source = Path(__file__).with_name('sweet_ufs.c').read_text()
for name, fields in m.FIELDS.items():
    fmt = 'NORD2 UFS ' + name + ': ' + ' '.join(k+'=%llu' for k in fields) + r'\n'
    assert fmt in source, (name, 'candidate report schema changed')
raw = '''NORD2 UFS STRATEGY: buffers=3 reads=2 requested_bytes=16385 rmw_reads=1
NORD2 UFS PHASES: map=10 mutex_wait=100 mutex_wait_max=60 held=200 copy=40
NORD2 UFS CALLERS: raw=3 cluster=2 pageio=2 async=3
NORD2 UFS SIZES: le4k=1 le8k=2 le16k=0 le32k=0 le64k=0 le256k=0 le1m=0 larger=0
NORD2 UFS PROFILE: freq=100 commands=5 reads=3 writes=1 syncs=1 read_bytes=32768 write_bytes=4096 cmd_ticks=1000 cmd_max=400 doorbell_ticks=900 doorbell_max=300 errors=0 timeouts=0
'''
r=m.parse(raw)
assert r['valid'], r
assert r['seconds']['mutex_wait']==1 and r['seconds']['copy']==.4
assert r['raw']==raw and r['actual_timer_hz']==100
assert m.parse(raw.replace('\n','\r\n'))['valid']
assert m.parse('prefix '+raw)['valid']
for bad in [raw[:-1], raw[:-2], raw + raw.splitlines(keepends=True)[0],
            '\n'.join(raw.splitlines()[1:])+'\n',
            raw.replace('freq=100','freq=0'),
            raw.replace('le8k=2','le8k=3'),
            raw.replace('requested_bytes=16385','requested_bytes=8194'),
            raw.replace('requested_bytes=16385','requested_bytes=20481'),
            raw.replace('reads=2','reads=4',1),
            raw.replace('raw=3','raw=4'),
            raw.replace('mutex_wait_max=60','mutex_wait_max=101'),
            raw.replace('copy=40','copy=201'),
            raw.replace('freq=100','freq=18446744073709551616'),
            raw.replace('copy=40','copy=40 INTERLEAVED'),
            raw.replace('reads=2','reads=3',1)]:
    result=m.parse(bad)
    assert not result['valid'], bad
    assert result['raw']==bad
    assert 'seconds' not in result
# Losing final newline and final digit must never become a smaller valid value.
assert not m.parse(raw.replace('timeouts=0\n','timeouts=40')[:-1])['valid']
print('PASS: actual-frequency conversion, request bounds, histogram/flags, raw evidence, missing/duplicate/truncated/overflow rejection')
