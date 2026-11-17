# Focused regression proposals (NOT executed, NOT written as code)

These tests use the existing actual-owner harnesses: the power `nord2-wifi-power-cleanup/owner.c`
and the handover `nord2-wifi-handover/owner.c`. Those run real service, transaction, crypto,
transport and descriptor code against synthetic hardware. "Expect" describes the required
outcome, not an observed one. None of these tests weakens an ownership check: each keeps the
existing pre-DDONE and unknown-DMA hard failures.

## T1. Exact hardware replay: OFF during an associated scan, deauth never acknowledged

Setup, mirroring the log:
- WPA2 connected on 5 GHz channel 52 with data flowing.
- An associated cid 3 scan of channels 1-13 is RUNNING when POWER OFF arrives.

Steps:
1. Cancel cid 4. Give it DDONE, but never deliver SCAN_DONE.
2. Give the deauth DDONE. Never deliver a TXS for its PID.
3. Then, in turn:
   - (a) send no firmware events for 3.5 s;
   - (b) deliver the late SCAN_DONE (0x0d) for the old sequence;
   - (c) deliver a late TXS for the deauth PID after cleanup has started.
4. Run once with canonical code (3 s local-timeout path) and once with the power candidate
   (immediate 257).

Expect for both runs:
- DELETE_PTK, DELETE_GTK, 0x14, 0x12 and 0x17 all go out.
- LEFT then RETIRED are delivered.
- SCANRETURN is sealed.
- `nss_native.failed` stays 0 and there is no STATIONFAULT.
- ON is followed by a fresh scan and a successful rejoin.

If canonical fails here, the STATIONFAULT `source` and `error` values identify hypothesis 1, 3 or
4 from REPORT §B. Variant: inject EVENT 0xF0 or 0xFD during step (a). Expect a single
STATIONFAULT with error=21 and a retained drain; never a fake success.

## T2. Combined handover + power merge

- Cases: SET22, then SET20 for network B while network A's deauth gets DDONE without TXS.
  Repeat with the associated scan running (T1 composition).
- Expect:
  - A's teardown completes first;
  - a fresh scan runs;
  - B prepares with a fresh 8 s budget;
  - A's LEFT is not published as B;
  - each cookie releases exactly once.
- After the merge, assert every `njs_fatal_owner(source)` call site still carries a distinct
  source.
- Assert the pending handover is canceled inside the fatal path *after* STATIONFAULT is printed.

## T3. Repeated OFF/ON and client ticket identity

1. OFF while associated, then ON within 50 ms, then iOS SET20 auto-join before old RETIRED.
   - Current proposal: returns 16.
   - Expect after the A6 change: SET20 is accepted as a handover, the old ticket moves to the
     retiring slot, and two releases balance.
2. Repeat OFF → ON → OFF → ON inside the old cleanup window and during the refresh scan.
   - Expect: there is never more than one refresh in flight.
   - Expect: `scan_state` ends at 1 or 3, never stuck at 2.
   - Expect: the ON auto-refresh re-arms (canonical `service_bind.inc:126-130`).
3. AP-initiated deauth (EVENT 0x13 / mgmt 0xc0), then SET20 to the same SSID before
   retirement, with no SET22.
   - Expect a handover or Busy, not NotReady (A6).

## T4. GTK rekey and data during cancel

1. OFF arrives while a group M1 is consumed and the REKEY_GTK cid 7 is wired but unpublished.
   Repeat with it published but without DDONE, and with DDONE but no M2 TXS.
   - Expect: the GTK index is in the delete set.
   - Expect: nsr ledger `ptk == gtk == 0` before SCANRETURN.
   - Expect: no error 10.
2. OFF arrives with a data frame published on the normal ring, DDONE seen, and the type-6 report
   delayed by 2 s, then by 9.9 s, then by 10.1 s.
   - Expect: the first two cases complete.
   - Expect: the third case is a deliberate error 3 with a retained drain. Document whether the
     product accepts this or needs a bounded report-loss policy similar to the deauth policy.

## T5. Native scan authority vs the UI history

Setup:
- The handover is pending.
- The fresh refresh scan is usable but does *not* contain the target BSSID.
- The IOUC 30 s history still lists the network.

Expect:
- JOIN_FAILED with result 2 and event 9.
- No prepare and no use of the UI cache.
- The request is wiped and the old cookie is untouched.

Then deliver a later UI scan that contains the target, followed by an explicit new SET20.
Expect success.

## T6. PID ceiling and late TXS (pins A5)

1. Loop connect/disconnect cycles with the real allocator.
   - Expect today: refusal when `next_pid > 27`, around cycle 10 (record the exact count).
2. If PID reuse with quarantine is added:
   - deliver a stale TXS for the reused PID from generation N-1 while generation N's
     auth/deauth is `air_pending`;
   - expect it to be ignored;
   - expect authorization to require the generation-N TXS.
   - Also cover a stale TXS arriving inside the quarantine window.

## T7. Fault reader on a real saturated log

Feed the actual post-merge owner log, with STATIONPART already saturated at 110, into
`nord2-wifi-station-fault.py`.

Expect:
- exactly one 3-part bundle;
- `error` is present;
- if the REPORT §B addition is adopted, the last firmware event ID is present.
