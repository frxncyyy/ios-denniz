# Compute DELTA17 — C6 audit fixes (host only)

No phone/native/publication. Production changes are limited to the compute
bridge and command failure path.

## Fixes

1. **Buffer-table fill error return**
   `metal/nord2_metal_bridge.c`: a missing/short extension-buffer record during
   table assembly now sets `e=BAD` before `goto cleanup`. It can no longer
   fall through cleanup with `e==0` and report success without submitting.

2. **FAU count refusal cleanup**
   `fau_count>128` now goes through `cleanup`, which releases the allocated
   command/table storage and drops TLS/WLS BOs. No early return leak.

3. **First command NSError retained**
   `metal/tegu/compute_frontend.m`: command execution installs a
   `TeguCompute` NSError only when `failure` is empty. `encodeSignalEvent:value:`
   retains its first NSError, never signals a foreign event, and later
   execution cannot overwrite/leak it.

## Host tests

- `python3 workers/compute/check_frontend_delta5.py`
  - four release configurations PASS
  - one ASan/UBSan configuration PASS
  - new event test keeps the first NSError across a foreign event and
    `teguExecute`, and verifies the command remains invalid.
- `python3 workers/compute/check_compute_v2.py`
  - release pipeline-format, synthetic bridge, and real corpus buffer-table
    tests PASS
  - one ASan/UBSan set for all three PASS
  - extended table fill, FAU pointer/records, spans and retained-write handles
    are exercised. The missing-record and `fau_count>128` branches are
    defensive paths; the table/refusal tests exercise the surrounding cleanup.

## Hashes

- `metal/nord2_metal_bridge.c`
  `9deda834199b1ec68339fea63f35a50e9ebddca6c6d46c83c78ba783a30f0a93`
- `metal/tegu/compute_frontend.m`
  `2902428c22286a5a78b38838c6f09a57154a583290162f7b699348444866f31d`
- frontend test
  `203bd546b378607cd533c7bff236b154b1eaa916adf72c672d3f08e6da9c796f`
- frontend runner
  `4f07ad08fc1fd4fbc6da1bc62b00826ce4fa6ec6dee4db2fb26ae8ef4352a197`

## Gaps

- No hardware/native execution.
- `N2C_JOB_BOS=32` transport cap and exact buffer-backed texture support remain
  unchanged from earlier deltas.
