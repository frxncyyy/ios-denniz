# Private handover candidate

Use runtime.patch contextually with the parent's power fix; promotion.json gives exact baseline/candidate hashes. Four runtime/header paths, no new TU. Do not copy private verification/generated test bulk or retained board data.

After promotion (or add --candidate pointing at proposed/shim/nord2-xnu):

```
python3 tools/test-nord2-wifi-handover.py --board /path/to/retained/WIFI --bearssl /path/to/sweet_bearssl --log-dir /tmp/handover-owner
python3 tools/test-nord2-wifi-handover-unit.py --log-dir /tmp/handover-unit
python3 tools/test-nord2-wifi-handover-client.py --log /tmp/handover-client.log
```

All tests are offline. Actual physical ordering evidence is numeric-only in timeline.json; full original log stays private at its existing immutable path. Parent source HEAD62fa3c0, physical runtime0d9a493. Candidate is not device-tested. Development prepare.py is not a reproducible generator for subsequent reviewed amendments and is excluded from promotion; exact proposed files/runtime.patch are authoritative.
