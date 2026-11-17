# OFF-cleanup error12: regression-test proposals (Claude, not executed)

None of these tests have been run. They are written against the fixture style in
`tools/fixtures/nord2-wifi-station/` (`transport-hardware.inc`, `test-security-submission.c`,
`test-native-air-timeout.c`). The parent implements and runs them.

## Prerequisite fixture change (needed by T2/T3)

`transport-hardware.inc:23` `readreg()` always returns `3` for `0x180600f0` and returns exact
`cidx`/`didx`. That means no existing test can show a firmware ready-bit drop or an index
mismatch, and the earlier "CID4 DDONE without SCANDONE" passes held by construction.

Add these fields to `struct test`:
- `uint32_t sw_sync` (default 3), returned for `0x180600f0`.
- `unsigned fw_off`. When a command descriptor whose captured wire byte 36 is `0x04` reaches DDONE
  (`dma_retire`), set `fw_off=1`, `sw_sync=0`, and stop generating TXS/events. This mirrors
  Android's power-off contract (`hal_pdma.c:3402-3427`: `NIC_POWER_CTRL`, then wait for the ready
  bits to clear).
- One-shot overrides `cidx_skew` and `didx_skew` for subterm injection.

## T1: wire-CID oracle (pure, strongest cheap guard)
- Drive a real associated scan and cancel through `nst_associated_scan` → `nst_associated_scan_cancel` → `nst_step`.
  Capture the command buffer and assert byte 36 == `0x1b`. Assert bytes 64..67 == `{scan_seq,0,0,0}`,
  which matches `struct CMD_SCAN_CANCEL` (`wsys_cmd_handler_fw.h:1175-1179`).
- Table test over the `cid_size` allowlist (`wifi_station_transport.c:52`). Each accepted CID must equal
  the gen4m `ENUM_CMD_ID` value by name (`wsys_cmd_handler_fw.h:176-205`): SCAN_REQ_V2 0x03,
  ADD_REMOVE_KEY 0x07, SET_RX_FILTER 0x0a, BSS_ACTIVATE_CTRL 0x11, SET_BSS_INFO 0x12,
  UPDATE_STA_RECORD 0x13, REMOVE_STA_RECORD 0x14, INDICATE_PM_BSS_CONNECTED 0x16,
  INDICATE_PM_BSS_ABORT 0x17, SET_BSS_RLM_PARAM 0x19, SCAN_CANCEL 0x1b, CH_PRIVILEGE 0x1c.
- Negative case: `cid_size(0x04,4)==0`. No station path may ever emit 0x04 (NIC_POWER_CTRL). Grep-level
  CI assertion: no literal `4` is passed as a CID argument to `command()`.

## T2: exact hardware replay, red under old CID, green under fix (key regression)
Sequence, through the real native composition (`init_join` → authorized WPA2, gen=2):
1. Associated scan published (cid3) and still active (no 0x0d completion).
2. `nord2_wifi_power_set(0)` path: `njs_disconnect_notice_locked` sets leave, the slot is canceled,
   and the owner cancels the scan (`wifi_station_service_bind.inc:330`).
3. Cancel DDONE → detach → `nwn_cancel` → cleanup deauth published → DDONE with no TXS → LOCAL_RETIRE 257.
4. Next owner pass: deletePTK.

Assertions:
- **Old-CID variant** (temporarily force cancel CID 0x04 in a test-only build): with the `fw_off`
  model you get `error==12`, `halt==1`, `transaction==STC_REMOVE_PTK`, `submitted==dma_completed`,
  `wire_busy==1`, `published==0`. This reproduces log lines 7179-7181 and proves the model is faithful.
- **Fixed variant**: the wire never carries 0x04. Cleanup reaches `STC_DONE`, with no halt, no `init.transport.dirty`,
  `!sta_active`, PTK/GTK wiped, and the ticket retired.
- Repeat with deauth TXS arriving at 0, 2, 50 and 900 ms after DDONE, and never. A late TXS after
  LOCAL_RETIRE must not complete anything (the PID is never reused).

## T3: error12 subterm discrimination (diagnostic contract)
At the deletePTK prepublication point, inject exactly one fault per case:
`sw_sync=1`, `sw_sync=0`, `cidx_skew=1`, `didx_skew=1`, coherent bit `0x00100000`, `reg_read` failure, and
owner failure (`fail_at`). Assert:
- `s->error` stays 12 (compatibility).
- New fields `guard_stage`, `guard_raw` and `guard_expected` (and `first_error` where an inner `fail()` ran
  first) identify the subterm uniquely.
- STATIONFAULT gets a new part that prints them. The reader in T6 parses that part.

## T4: OFF during scan, then ON and rejoin (service level)
After T2 (fixed), call `power_set(1)` and check:
- The refresh scan is scheduled (`nsr_native.scan_state`).
- The canceled slot is delivered exactly once.
- The IOUC retiring ticket was retired at OFF (`wifi_ios_client.cpp:218-220`), so the next join is
  admitted only after the real scan-return barrier, and the management PID budget advances by the expected amount.

## T5: parity with the known-good handover
Explicit SET22 disassoc with no associated scan (the hardware success at log 6288-6416) must still
produce TXS-before-retire or LOCAL_RETIRE, then cleanup and `SCANRETURN`, unchanged by the CID fix.

## T6: frozen readers
Feed the existing `off-on-final-after-crash.log` to the fault reader. It must still parse parts 1-3,
and must report missing subterm fields as "not recorded", not as a pass. Add a synthetic line with
the new fields.

## Hardware (parent's decision, single cycle each, after the test image is verified)
1. Connected 5 GHz, Settings Wi-Fi page open (an associated 2.4 GHz scan in flight is likely), OFF, wait 5 s, ON.
   Expected: deauth `seen=1`, or LOCAL_RETIRE with no STATIONFAULT; cleanup completes; ON rescans and rejoins.
2. Only if 1 passes: OFF with no scan in flight.
