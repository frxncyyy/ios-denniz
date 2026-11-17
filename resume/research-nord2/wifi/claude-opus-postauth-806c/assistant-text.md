Let me locate the key files with narrower searches.

Globbing is timing out on this tree. Let me try reading expected files directly by path.

The tree is slow to glob. I'll read the canonical files directly from `shim/nord2-xnu/`.

Core TXD/frame composition captured. Let me read the headers and the WPA/transport sources, plus find the log.

The event routing is clear. Now the WPA engine and transaction state machine, plus locating this task's log.

WPA engine analyzed — the post-authorization gap is clear. Now I need the Android TXD ABI for comparison, the key-field layout, and this task's log.

Now I have the full event timeline. Let me read the Android TXD implementation and the HW descriptor bit definitions to compare against the shim's `txd()`.

Android reference composition captured. The exact bit mapping is decisive — let me read the connac2x TXD HAL macros and the data TX path (`nicTxFillDesc`/`secIsProtectedFrame`).

Now the exact bit mapping. Let me pull the `HEADER_FORMAT_*` enum values, the 802.11 header-length field, and the packet-format enum.

Key mapping confirmed. Let me nail the `HEADER_FORMAT` enum values, TXD padding/length constants, and the chip `pse_header_length`.

The descriptor mapping checks out. Now let me confirm how ordinary **data** egress is wired in the shim (which TXD builder, which ring/PID) vs. the real data path, and how EAPOL/data submit works.

Data egress confirmed: the shim sends ordinary data via `sw_data_txd` (PACKET_FORMAT=COMMAND) on the single **command ring** (PID 127). Let me verify the Android connac2x packet-format for data and the PSE header constant, then confirm the post-auth EAPOL failure propagation.

Both primary causes are now well-grounded. Let me confirm the post-auth EAPOL failure propagation (`nwo_eapol`/`STC_FAILED`) and read the diag field semantics so I can propose a precise reserved diagnostic.

H2 is fully confirmed (`stc_eapol`→`nwp_receive`→`NWP_FAIL`→`stop(STC_FAILED)`→deauth). Let me read the diag definitions to precisely interpret the TXS/event records and ground a reserved-diagnostic proposal.

Now I can decode the diagnostics precisely. Let me confirm exactly when the reference driver uses `TXD_PKT_FORMAT_COMMAND` (vs data), to make the composition-mismatch claim airtight.

I now have a complete, evidence-grounded picture. Writing the final diagnosis (Root will save it as REPORT.md).

---

# REPORT.md — Independent read-only diagnosis: iOS16 Wi-Fi join on OnePlus Nord2 (MT6893)

Scope: source/log audit only. Runtime source `38b3e67`, boot image `806c8d61…`, CI `39841085457`. Inputs: `research/nord2/wifi/claude-opus-postauth-806c/numeric-events.log`; canonical `shim/nord2-xnu/*`; reference tree `../nord2-current-kernel` gen4m (`drivers/misc/mediatek/connectivity/wlan/core/gen4m`). No secret material reproduced. I did not touch the known scan-return guard (other agent's item).

## Summary of findings

Two independent, source-confirmed blockers explain the two observed symptoms. They are distinct and should ship together.

- **B1 — No usable IP / MPDU_ERROR:** ordinary (protected) **data is composed with the EAPOL/command TXD template** (`PACKET_FORMAT = TXD_PKT_FORMAT_COMMAND = 2`) and dispatched on the **command ring**, with the PROTECTED_FRAME bit set. The reference driver sends protected data only via the **data path** with `PACKET_FORMAT = TXD_PKT_FORMAT_TXD = 0`. The firmware rejects the mismatched protected COMMAND-format MPDU → host PID127 TXS status 3 (MPDU_ERROR) → no encrypted data egresses → no DHCP → spinner, no IP.
- **B2 — Spontaneous late drop at 215 338 ms (user did NOT toggle Wi-Fi):** the WPA engine has **no post-authorization EAPOL handling** (no group-key/GTK rekey, no PTK re-handshake). Any such frame hits the catch-all `fail()` in `nwp_receive`, which returns `NWP_FAIL` and **wipes all key state**; the transaction turns `STC_AUTHORIZED → STC_FAILED` and tears the link down.

---

## B1 — Protected data uses the command/COMMAND template, not the data path

### What the shim does
- TXD composer hard-codes the packet-format field for every frame:
  - `shim/nord2-xnu/wifi_station_wire.c:190-191` — DW0 `… | (2u<<23) | …` sets `PACKET_FORMAT = 2`.
  - `wifi_station_wire.c:206` `sw_data_txd → txd(…, security=2)`; `:204` security(=EAPOL); `:203` management — **all three** go through the same `txd()` and all set `2u<<23`.
- Ordinary data egress:
  - `wifi_station_transport.c:63` `nst_data → frame(…, type=3, protect=1)`.
  - `:54` sets `d.pid = NST_PID_HOST` and `d.protected_frame = 1`; `:55` calls `sw_data_txd`.
  - `:97` the composed wire is DMA'd to `s->init->transport.command` (the **command ring**, producer reg `0x18025418`). There is only one TX ring in the shim (`nst_memory_range` `:64-71` references `transport.command`, `tx_ring`, and the RX rings — no separate LMAC data ring).
  - `NST_PID_HOST = 127` (`wifi_station_transport.h:16`); host security+data all share PID 127, disjoint from management 1..126.

### What the reference requires (MT6893 = connac2x, PCIe/WFDMA)
- Packet-format field is DW0 bits[23:24] (`include/nic/nic_connac2x_tx.h:93-94`), enum `TXD_PKT_FORMAT_TXD=0 … COMMAND=2` (`include/nic/nic_tx.h:437-440`).
- Chip default for connac2x data = **TXD(0)**: `chips/common/cmm_asic_connac2x.c:175` `ucPacketFormat = TXD_PKT_FORMAT_TXD`.
- Selection at fill time (`nic/nic_tx.c:1972-1975`):
  ```
  if (prMsduInfo->eSrc == TX_PACKET_MGMT) ucPacketFormat = TXD_PKT_FORMAT_COMMAND;
  else                                    ucPacketFormat = prChipInfo->ucPacketFormat;  // = TXD(0)
  ```
- COMMAND(2) is used **only** for MGMT-sourced frames and the explicit "command-data" path (`nic_tx.c:3418`), and for unprotected EAPOL security frames (`nic_txd_v2_compose_security_frame`, `nic/nic_txd_v2.c:735`). Ordinary data uses `nic_txd_v2_compose` (`:453-454`, `PACKET_FORMAT = ucPacketFormat = TXD`) and is queued on a data ring where LMAC applies the WCID/PTK CCMP key.

So the shim reuses the EAPOL/command layout for ordinary **data** — exactly the "incompatible layout" the task warns against.

### Log evidence (decoded via `wifi_station_diag.c`)
NSTDIAG fields: `a=v0 b=v1 c=v2 d=v3 e=v4 f=v5 h=v6 i=v7`. For TXS records (`k=4`, `nstd_txs` `wifi_station_diag.c:11`): `d = p[36]` (TXS PID), `f = p[37]` (**raw firmware TXS status byte**), `h` = driver air-wait class.

- Management frames (handshake): `n=21` `k=4 d=1 f=0 h=7`, `n=25 d=2 f=0 h=7` → **status 0 (acked)**.
- Every post-authorization host frame (PID 127): `n=72,73,78,85,95` `k=4 d=127 f=3 h=3` → **status 3 = MPDU_ERROR** (per the selected Android TXS enum). These appear right after port authorization (168 497 ms) and recur.

Other corroboration: `tx_ok=0 / tx_fail=0` throughout (no typed air result for host frames, only the ambiguous PID127 TXS); `rx_delivered` climbs to 232 but the task's own caveat applies — that is not DHCP proof. iOS getters return success (`GET 21`, join-up bundle producer 588 consumed), consistent with association succeeding while **data never completes**.

### What is proven vs. unknown
- Proven (source): data is composed with COMMAND format + PROTECTED on the command ring; reference uses TXD format on the data path. Definitive divergence.
- Observed (log): firmware TXS `p[37]=3` on PID127 vs `0` on management — the firmware treats the post-auth frames differently and rejects them.
- Unknown: the exact firmware-internal reason; whether a given PID127 TXS belongs to M4 vs ordinary data (PID 127 is shared, so per-frame correlation is impossible); whether LMAC CCMP could succeed on the command ring if only the format constant changed.

---

## B2 — WPA engine has no post-authorization EAPOL state

### Source
`shim/nord2-xnu/wifi_wpa2.c` `nwp_receive`:
- Only two accepted key-info values once past M1:
  - `info==0x008a` (4-way M1, pairwise) — `:135-146`.
  - `info==0x13ca` (M3) — `:147-164`, incl. a strict M3-retransmit branch (`:150-157`, higher replay counter, otherwise byte-identical).
- Catch-all `:147` `if(info!=0x13ca || w->state<NWP_M3) return fail(w,r);`. In `NWP_AUTHORIZED`, a **group-key handshake msg1** (CCMP group, secure+MIC+ACK, encrypted GTK KDE — key-info e.g. `0x1382`, i.e. group bit clear, no install bit) is neither `0x008a` nor `0x13ca` → `fail()`. A fresh 4-way restart (`0x008a` while state≠M1/M3) also → `fail()` (`:137-138`).
- `fail()` (`:72`) calls `nwp_cancel()` → **wipes the entire `nwp` state (keys included)** and returns `NWP_FAIL`.

### Propagation to teardown
- `wifi_station_transaction.c:138-139` `stc_eapol`: `result = nwp_receive(...)`; `if(result<0) return stop(s, STC_FAILED);` (only `NWP_DROP/BUSY` are ignored at `:139`; `NWP_FAIL` is fatal).
- `stop(…, STC_FAILED)` / `halt()` (`:17`) sets `STC_AUTHORIZED → STC_FAILED`, erases queued EAPOL and `nwp_cancel`, drives deauth/cleanup.

### Log evidence
- `n=96` at 215 338 ms is `k=8` (teardown **summary**, emitted from `nst fail()`), not a `k=5` EAPOL parse — see "reserved diagnostic" below.
- Teardown sequence at 215 338–215 362 ms: `service=7 result=2` (FAILED) → actions `10/11/12/13` = `STC_TX_DEAUTH / STC_DELETE_PTK / STC_DELETE_GTK / STC_RETIRE_PEER` → `IFNET LINK authorized=0` (line 423) → `JOINNOTICE phase=8 result=2 authorized=0`, `retired=1`. The down-event bundle (producer 764 / consumer 588) originates in the driver, confirming a **driver-initiated** teardown, not an iOS toggle.

### Ruling out the decoys (as the task asks)
- `POWERSET state=0 unsupported` at 26 873 ms and `request=19 unsupported` are pre-join, benign.
- `DISASSOC reason=8` at 168 279 ms **precedes** `SET 20` (the join, 592 B) at 168 295 ms — it is iOS clearing prior state as part of the join, not the 215 s drop. There is no post-join `SET 22/19` before the drop. So the drop is not iOS-initiated.

### Proven vs. unknown
- Proven (source): no group-key/PTK-rekey path exists; any such frame → `NWP_FAIL` → `STC_FAILED` → key wipe + deauth. This will tear down **every** connection that survives to the AP's first GTK rekey.
- Unknown (log): the exact key-info/subtype of the 215 s frame (group-rekey vs 4-way-restart vs malformed M3) — because the 96-record NSTDIAG budget was exhausted by ordinary TX diag by ~170 s, the discriminating `k=5` record was never emitted. The fix is identical regardless of which of these it was.

---

## Why the diagnostic couldn't prove the subtype (and the minimal reserved fix)
- `wifi_station_diag.c:17` caps total records at 96. Each TX frame emits 3 records (`k=1/2/3`, lines 101/108 of transport); handshake + post-auth data spam exhausted the budget by ~170 s, so the single remaining slot at 215 s was the teardown summary (`k=8`), not the EAPOL parse.
- `nstd_eapol` (`k=5`, `wifi_station_diag.c:12`) already extracts **non-secret** header fields: `v[3]` = key-info (`p[5]<<8|p[6]`), `v[4]` = classification (`0x008a→1, 0x13ca→3, else 0`), `v[6]` = length-consistency. It does **not** log nonce/MIC/key-data.

Recommended reserved diagnostic (no secret dumps):
1. Reserve a tiny dedicated budget (e.g., 4 slots) for `k=5` records emitted while `outcome==STC_AUTHORIZED`, outside the global 96 cap — or stop emitting per-frame `k=1/2/3` data-TX diag once authorized (they are the budget hog).
2. For any post-auth EAPOL, always record `v[3]` (key-info), the key-type bit (pairwise vs group), and the sign of the replay-counter delta. These three plaintext values discriminate group-rekey (`~0x1382`) vs 4-way-restart (`0x008a`) vs M3-retransmit (`0x13ca`) without touching secret material.

---

## Prioritized recommendations (one cohesive next build)

1. **B1 (highest — restores data/IP).** In `wifi_station_wire.c` `txd()`, set DW0 packet-format per path: keep `COMMAND(2)` for management (`security==0`) and EAPOL (`security==1`), but use `TXD_PKT_FORMAT_TXD(0)` for data (`security==2`). Necessary but likely **not sufficient**: the shim currently has only the command ring, whereas MT6893 hardware CCMP for data runs on the LMAC **data path**. Verify (source) that a TXD-format protected MPDU placed on `transport.command` is actually consumed by LMAC; if not, a real data ring / data-path dispatch is required for the WCID/PTK key to be applied. Do **not** relax the PROTECTED_FRAME bit or fake a positive host-air outcome.
   - Test: capture the PID127 TXS `p[37]` after the change; success is `status==0` on a data frame plus an observed bidirectional DHCP exchange — not a local DMA/SUBMITTED receipt (per the standing caveat, local receipt ≠ air ACK ≠ IP).

2. **B2 (prevents the ~215 s drop).** Implement the WPA group-key handshake in `wifi_wpa2.c nwp_receive`: add a branch for group msg1 in `NWP_AUTHORIZED` that strictly verifies the KCK MIC and a monotonically increasing replay counter, decrypts the GTK KDE with KEK, installs the new GTK via the existing `STC_CID_GTK` path, and replies with group msg2. Add the analogous strict path for an AP-initiated 4-way PTK rekey. Do **not** merely drop/ignore the frame, and do **not** weaken MIC/replay checks. Until implemented, the connection cannot survive the first rekey.
   - Test: once B1 gives a stable link, let it run past the AP's GTK rekey interval; expect the link to stay `authorized=1` and a `k=5` record showing the group msg1→msg2 exchange and a GTK reinstall.

3. **Diagnostics (ship with the above).** Apply the reserved `k=5` budget change so the next hardware run can positively identify the rekey subtype and prove B2's fix.

Secondary TXD fields I checked and found **consistent** with the reference (not blockers): ether-type offset `(12+pse)>>1` with `pse_header_length==8` (`wire.c:190`, `cmm_asic_connac2x.h:101`, `nic_txd_v2.c:685-688`); HEADER_FORMAT = NON_802_11(0)+ETHERNET_II(bit15) for data/EAPOL and 802.11-normal(2)+hdr-len for management (`nic_tx.h:276-281`, `nic_connac2x_tx.h:106-115`); PROTECTED_FRAME = DW3 bit1 (`:164`); fixed-rate-enable = DW2 bit31 (`:160`); TX byte count = 32+len; unprotected pairwise EAPOL (matches "Pairwise EAPoL not protect", `nic_txd_v2.c:436-443`). PN/sequence are HW-assigned and not a factor.

Bottom line: **B1 is the strongest concrete cause of MPDU_ERROR / no IP; B2 is the cause of the spontaneous late drop.** Both are source-proven; the only residual unknowns (exact firmware reject reason, per-frame PID127 correlation, exact late-EAPOL subtype) are addressable with the narrow reserved diagnostic above and do not change either fix.
