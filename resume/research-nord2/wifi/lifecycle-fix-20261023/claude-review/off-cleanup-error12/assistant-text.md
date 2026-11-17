Status: I've read the fault block in the log (lines 7149–7199). Next I'm locating the error‑12 guard and the deauth→deletePTK cleanup path in the source.

The reboot is the driver's own deliberate response: line 7199 is "panic pending" 30 s after a retained halt. Now finding the source.

Still locating the runtime source tree (prior review reports give file names but not paths); then I'll read the guard.

Now tracing `nord2_wifi_power_set(0)` to see what OFF does at the hardware level while the station cleanup is still running.

Status: writing up soon. One last check: the types of `submitted`/`dma_completed`, to rule out any 8-bit wrap in the lease check.

Independent confirmation complete. The only other CIDs in the allowlist (`transport.c:52`) match gen4m enum names. Writing the report and test proposals now.

# OFF-cleanup error12: independent review (Claude)

I only read source and logs. I ran no commands, builds, tests, Git, CI or device actions. I wrote two files in `claude-review/off-cleanup-error12/`: `REPORT.md` (this text) and `TEST-PROPOSALS.md`.

Line numbers refer to the working tree as I read it, and **that tree changed during this review**. In `shim/nord2-xnu/wifi_station_transport.{c,h}`, the literal `4` became `NST_CID_SCAN_CANCEL 0x1b`, so someone is already editing it. What the runtime actually did is proven by the log, not by this tree.

## Likely root cause
The runtime sent its "associated-scan cancel" as **wire CID 0x04**. In the selected Android gen4m driver, 0x04 is **`CMD_ID_NIC_POWER_CTRL`** (`wsys_cmd_handler_fw.h:181`). The real scan cancel is `CMD_ID_SCAN_CANCEL` = 0x1b (`:204`, used by `scan_fsm.c:543-561`).

Android's PCIe power-off path sends NIC_POWER_CTRL and then waits for the SW_SYNC0 ready bits to clear (`hal_pdma.c:3402-3427`, `wlan_lib.c:1646-1673`). Our payload `{scan_seq,0,0,0}` (`transport.c:335`) puts a non-zero value in `ucPowerMode` (`nic_cmd_event.h:1553-1556`). Otherwise the 4-byte layout is identical to `CMD_SCAN_CANCEL` (`wsys_cmd_handler_fw.h:1175-1179`), which is why the error went unnoticed.

Expected effect, which matches both crash logs:
- The firmware enters power-off.
- The deauth is fetched by DMA (DDONE) but never transmitted, so there is no TXS.
- The firmware clears the `0x180600f0 & 3` ready bits, so the next command fails its pre-publish check with **error 12**.

## Proven (source + log)
1. **The reboot is the driver's own response.** After the halt, `failure_drain.inc:14-24` drains for 30 s and then panics (log 7199). "ON failed" is NotReady (`0xe00002d8`, log 7206-7217) during that drain.
2. **The runtime put CID 0x04 on the wire.** Log 7156 shows `cid=4 sequence=117 submitted=254 dma=254` right after POWERSET 0 (log 7150). The CID byte is written at `wire[36]=cid` (`transport.c:66`).
3. **Error 12 can come from only three checks.**
   - The only error-12 site is `transport.c:175`, reached only via `nst_step` → `transmit` (`:336`).
   - `fail()` overwrites `s->error` (`:12`), so an inner `before()` (codes 1-5) or `coherent()` (code 11) failure would also be reported as 12.
   - But `before()` and `coherent()` passed microseconds earlier at `:336`, on the same thread, under the same lock, with the same state.
   - Every register read also goes through `ncd_native_owner` → `nwfn_guard` (`wifi_capability_bind.inc:16-31`, `wifi_main_wfdma_bind.inc:191-249`). That guard records the first trip of fw-own, coherent bits, the DMA1 error-recovery interrupt bit (bit 29), and the wake/clock/sleep registers. STATIONGUARD is all zero, so none of these tripped.
   - **So the failing check is one of: SW_SYNC0 `&3`, CIDX `0x18025418`, or DIDX `0x1802541c`.** The raw value was not recorded.
4. **The firmware changed between two commands.** The same three checks passed about 2-3 ms earlier for both the cancel and the deauth. Management frames also go through `:175`; only types 2/3 take a different path at `:172`. Between the deauth's check and deletePTK's check, the host only wrote CIDX, observed DIDX/DDONE, wiped the buffer and advanced `tx_next` (`:183-203`). The change therefore came from the firmware, which fits power-off clearing the ready bits.
5. **The two logs split on one variable** (6 cleanup deauths):

   | Case | Scan state | Result |
   |---|---|---|
   | 4 explicit disassocs (crash.log 6398/7068/7547, final.log 6290) | no scan in flight | TXS `seen=1`, cleanup completed |
   | OFF in final.log 7159-7181 | associated 2.4 GHz scan in flight, cancelled | `seen=0`, halt 2 ms later at deletePTK |
   | OFF in user-switch-power-crash.log 8102-8159 | associated 2.4 GHz scan in flight, cancelled | `seen=0`, halt 3005 ms later, right after LOCAL_TIMEOUT 256 |

   In the old run, waiting 3 s for TXS did not help. That rules out a short race and fits firmware that is off. The old run's exact error is inferred, because that build had no STATIONFAULT.
6. **Natively, OFF differs from disassoc only by the scan cancel.** `power_set(0)` sets `powered=0`, cancels NSS slots, and calls the same `njs_disconnect_notice_locked` (`wifi_station_service_bind.inc:122-157`). It does no hardware power-down. The only native difference from disassoc is the scan cancel (`:330`).
7. **Deleting the PTK early is not the fault.** Android does not change the ready bits on deauth or key removal; it reads SW_SYNC0 only at init and off. It resets host ring indices only during host init/reset (`hal_pdma.c:2007-2012`, `2210-2212`). A firmware error recovery would have tripped guard 23. deletePTK is simply the first command after the firmware went off.
8. **Counters do not wrap here.** `submitted`, `dma_completed` and `tx_next` are `unsigned` (`transport.h:52`), and the index checks are modulo 1024. 255 is an aggregate count.
9. **The other CIDs are fine.** By name, every CID in `cid_size` (`transport.c:52`) matches gen4m (`wsys_cmd_handler_fw.h:176-205`). 0x04 was the only mismatch.
10. **No existing test could have caught this.** The fixture `transport-hardware.inc:23` hard-codes SW_SYNC0=3 and exact indices.

## Not proven
I believe the failing check was SW_SYNC0 (ready bits cleared by NIC_POWER_CTRL), not CIDX or DIDX, but the raw value wasn't recorded. The firmware's behaviour for `ucPowerMode` values 55 and 46 (the scan sequence numbers it received) is undocumented; Android only ever sends 1.

## Recommended correction (justified by the evidence; bounded)
1. **CID fix.** Send 0x1b with the same 4-byte payload. This is already in the working tree. Pin it with an oracle test (T1). No guard is weakened.
2. **Minimal diagnostic.** Keep `error=12`, but also record:
   - `guard_stage`: which check failed (read error or wrong value) for SW_SYNC0, coherent, CIDX or DIDX.
   - `guard_raw` and `guard_expected`.
   - `first_error`: the inner `fail` code that is currently overwritten.

   Capture these at the moment of failure, without re-reading after the halt, and print them as an extra STATIONFAULT part. Do the same for `fail(40)` (`:130`).
3. **Optional, not needed to fix the crash.**
   - Cleanup deauth now retires locally about 2 ms after DDONE (`:206-211`). That makes the existing bounded wait in `retired_air_failure` (`:116`, `:338`) unreachable.
   - Android waits up to 100 ms (1000 ms while scanning) for the deauth's TX-done before teardown (`ais_fsm.c:2476-2489`, `5525-5534`). With a real scan cancel, the firmware may need that time to get back on channel; otherwise the deauth may just not go out. This is courtesy to the AP, not a safety issue.
   - Also, `transmit` runs before `receive` (`:336-337`). Once a pre-publish check fails, any pending firmware assert or SCAN_DONE event is never decoded.

## Tests
They're in `TEST-PROPOSALS.md`. T2 is the key one: an exact replay of this OFF-during-scan sequence. It needs a fixture model where CID 0x04 clears SW_SYNC0 and stops TXS, so the replay fails with the old CID and passes with the fix. Don't mark OFF as fixed until one hardware OFF-during-scan cycle passes.
