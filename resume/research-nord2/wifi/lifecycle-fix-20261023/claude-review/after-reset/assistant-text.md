Reading the crash log now to map the switch and OFF timelines before I review the proposals.

Found the panic-pending drain in `failure_drain.inc`; tracing which path latches it after the OFF deauth.

Reading the power agent's proposed `wifi_station_service_bind.inc` to compare against canonical.

Cross-checking the Android gen4m reference for how scan-cancel and disconnect are ordered.

I'm still pinning down which check actually fails 3 s after the OFF. Checking the power agent's diagnostic runs for their reproduction.

The power agent just added a proposed `wifi_station_transport.c`; reviewing what it changes.

Now checking how the handover proposal's IOUC client side handles two live tickets (old retiring + new pending).

Found a likely deterministic limit: management PIDs are boot-global, never reused, capped at 31, and rejoin is refused past 27. Verifying against the log.

Checking the handover agent's latest verification results before writing up.

Re-checking both proposal directories for concurrent changes before I write the report.

Both agents have now published REPORT.md files. Reading them so my review addresses their final claims.

I have enough to write the review. Writing REPORT.md and a separate test-proposal file to the assigned output directory.

Final check that neither proposal changed after I read it.

I finished the independent review and wrote `claude-review/after-reset/REPORT.md` and `TEST-PROPOSALS.md`. I ran no commands, compiled nothing and executed **no tests**. I didn't touch the device, Git or CI, canonical source, or the other agents' folders. Nothing here claims hardware success.

I reviewed the canonical tree at d87ed97 (runtime 0d9a493) and both proposal folders, including their final `REPORT.md` files. I checked again before finishing; they hadn't changed since I read them. I couldn't compute hashes without a shell, so the parent should pin the exact files it merges.

**Confirmed by source and log**
- **First switch fails at both layers.** The app-side code returns Busy (16) while the old connection's ticket is still held (`wifi_ios_client.cpp:468`). The driver would also refuse until a fresh scan completes. On the phone that scan took about **7.1 s** (log 241304→248487). The handover proposal fixes both layers, but an accepted join will still wait about 7 s before it starts.
- **"ON doesn't work" is a symptom of the crash, not a separate bug.** After the fatal error, everything returns NotReady during the 30 s drain, then the device panics (`wifi_scan_service_bind.inc:144`, log 8159/8166).
- **The fatal error comes from the station failure handler (`njs_fatal_owner`), 3005 ms after OFF.** That matches the 3 s command deadline in `wifi_station_transport.c:180`.
- **The missing deauth acknowledgement was expected and is not by itself the crash.** At OFF the radio was scanning channels 1–13 while connected on 5 GHz channel 52. Android explicitly handles this case with a timeout and carries on (`ais_fsm.c:2476-2489`). Our code already treats it as non-fatal (`transport.c:112-117`, `326-335`), and the log shows the frame was handed to the radio before the crash. The power agent reproduced the same result.
- **New: hard limit of about 10 connections per boot.** Management frame IDs are never reused and capped at 31. Rejoin is refused once the counter passes 27 (`wifi_scan_return_bind.inc:80`, `wifi_station_transport_bind.inc:74`). The log shows 3 IDs used per connection (1 to 13 over 5 connections). Neither proposal changes this, and it will cap repeated switch or OFF/ON testing. Fixing it needs reuse with a cool-off period and tests for late acknowledgements.
- **Remaining handover gap:** after OFF→ON, or after the router drops the connection, a new join made before the old one finishes cleaning up still gets Busy or NotReady. Only an explicit disconnect moves the old ticket aside.
- An existing test, `service-test.c:161`, now conflicts with the handover proposal. It needs updating to match the new behaviour, not deleting.

**Still unknown: which exact check crashed.** I list four ranked hypotheses in REPORT §B, for example a later cleanup command failing, or a firmware assert event. The power proposal's immediate retire is a reasonable improvement that matches Android. It doesn't prove a fix: under most of my hypotheses the device would just crash about 3 s sooner. The next build needs the power agent's STATIONFAULT diagnostic. I suggest it also records the last firmware event ID.

**Tests the parent needs to run (T1–T7 in TEST-PROPOSALS.md):**
- T1: exact replay of OFF during a scan with no acknowledgement, through the real station code.
- T2: handover and power merged together.
- T3: OFF/ON and client ticket handling.
- T4: group-key renewal and in-flight data during cancel.
- T5: target network missing from the fresh scan.
- T6: frame-ID limit and late acknowledgements.
- T7: fault reader on a real log.

Separately, these need a phone test: how long iOS waits after an accepted join, whether iOS copes without the old disconnect events, rejoining the same router after OFF, and OFF while idle versus OFF during a scan.

Both proposals rewrite the same three functions in `wifi_station_service_bind.inc`: `njs_fatal_owner`, the top of `njs_owner_work` and `njs_disconnect_notice_locked`. The merge has to keep the fault-source code on every failure call and print the diagnostic first.
