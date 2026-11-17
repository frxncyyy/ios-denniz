# Revised verdict (replaces B1/B2 in the previous REPORT.md)

**Bottom line:** the previous report overstated both findings. Neither is a proven cause of this run's failures. B1 is withdrawn as a source-justified functional change. B2 is downgraded to a real but unattributed missing feature. **No functional TX change is source-justified before the next build.** The next decision needs the exact hardware evidence from the reviewed WPAHDR/reject-enum and TXS/protocol counters.

Caveat on line numbers: the working-tree `wifi_station_transaction.c` now contains the private-candidate instrumentation (`nwpd_observe`, `r.failure` at :140). Its line numbers are one off from the ones I quoted before. Citations below are to the current working tree, not necessarily to runtime `38b3e67`.

## B1 — withdrawn as a "source-proven blocker"

**What the source proves:**
- Android has a supported command-data route for ordinary OS data. `wlan_lib.c:4381-4399` calls `nicTxFillMsduInfo` and then `nicTxProcessCmdDataPacket`, and enqueues the result as `COMMAND_TYPE_DATA_FRAME`.
- `nic_tx.c:3394-3407` sets `eSrc=TX_PACKET_OS`, TC4, `TX_PACKET_TYPE_DATA` and no template, then calls `nicTxFillDesc`. Because `eSrc` is not MGMT, `nic_tx.c:1972-1977` goes through the normal composer.
- That normal composer applies protection whenever the PTK exists. `nic_txd_v2.c:426-435` sets `MSDU_OPT_PROTECTED_FRAME` when `secIsProtectedFrame(...)` is true and `prStaRec->fgTransmitKeyExist`. `nic_txd_v2.c:195-198` then sets DW3 PF. Header format is NON_802_11 plus ETHERNET_II (`:413-415`).
- `nic_tx.c:3418` then forces `TXD_PKT_FORMAT_COMMAND`, lifetime NO_LIMIT (`:3424-3428`), and a driver-assigned PID (`:3434-3439`).
- So protected Ethernet data in COMMAND format is a source-supported layout. My earlier claim that protected data goes "only via TXD(0)" was wrong. I did not read the body of `secIsProtectedFrame`, so the protection condition rests on the call site only.

**What is not proven:**
- Whether this route is active or valid for this radio and firmware. Per your citation it is gated on `CFG_SUPPORT_LOWLATENCY_MODE`, `ucLowLatencyCmdData`, firmware `fgTxDupCertificate`, `fgEnTxDupDetect`, packet-mark policy and `CHECKSUM_NONE`. Route existence is proven; applicability is unverified driver policy and firmware capability. The cut-through TXD(0) default is not the only valid ABI.
- I found no concrete source evidence that our template breaks a firmware requirement. Residual differences from the command-data template, none shown to be required:
  - finite lifetime vs NO_LIMIT;
  - fixed-rate-enable always set (DW2 bit31) vs AUTO for data;
  - TID 0 vs user priority;
  - fixed PID 127 vs `nicTxAssignPID`.

  These are things to tell apart on hardware, not changes to ship blind.

**Log, corrected:** there are six PID127 TXS records with `f=p[37]=3`, not five: `n=72,73,74,78,85,95`. In those records `h=3` is our own "not pending" classification, not a firmware result. PID127 is shared, so the six records can't be tied to M4, ordinary data, or "all egress". The MPDU_ERROR meaning comes from the selected Android enum; I did not re-derive it here.

Your configd evidence fits: a link-local 169.254/16 request with no DHCP add points to incomplete DHCP. It is captured at `in_control` entry, so it shows fallback activity, not an installed address. It does not attribute any TXS to a frame.

## B2 — downgraded to "missing feature, cause unproven"

**What the source proves:**
- The late `NST_EAPOL` reached `stc_eapol`. `STC_FAILED` has two possible sources there: `stc_eapol` `:134` (`!n || n>640`, which never reaches the WPA engine) and `:141` (`nwp_receive<0`). The public log doesn't distinguish them. That needs the reviewed reject enum.
- Valid M3 retransmissions are supported without reinstall: `wifi_wpa2.c:150-157` accepts only a same-length, non-decreasing-replay, otherwise identical body and re-sends M4.
- Group msg1, a fresh 4-way M1 after authorization, and invalid MIC, replay or content all reach `fail()` (`wifi_wpa2.c:72, 137-138, 147-149, 153-154`). These are separate branches.

**What is not proven:** which branch fired. Group-rekey support is genuinely missing, but it is not shown to be what caused this closure.

**Correction:** "the fix is identical regardless" was unsupported and withdrawn. Each branch needs different handling:
- **Group rekey:** needs per-key-ID GTK/WTBL and replay lifetime, rotation, and no reinstall on retransmission. The current single GTK replay slot and an overwriting `STC_CID_GTK` reuse are not safe enough.
- **Fresh M1:** needs a separately specified PTK rekey design.
- **Invalid MIC, replay or content:** whether to drop or close is a policy decision. Neither acceptance nor silent ignoring should be widened.

## Decision for the next build

- **Ship:** the already-reviewed diagnostics (reserved 8 early / 24 authorized / 8 failure WPAHDR plus exact reject enum, bounded protocol/TXS counters) and the safe post-authorized return to scans.
- **Don't ship yet:** any functional TX change (packet format, ring, lifetime, rate, TID or PID) or any group-key acceptance path.
- **What the next hardware run must show:**
  1. The late frame's key-info and reject enum. This tells group, M1, M3-retransmit and invalid-MIC/replay apart, and separates `:134` from `:141`.
  2. Per-class TXS status counts (security vs data) and positive or negative host-air outcomes.
  3. Whether any unicast DHCP reply is ever received.

  Only that evidence can justify a functional change.
