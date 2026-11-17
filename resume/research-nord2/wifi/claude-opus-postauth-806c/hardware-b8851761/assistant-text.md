Status: peer config (OP mode 0 = infra, STA type 65, AP-StaRec selection for broadcast DA) matches source; now checking the raw log's public DATATXS/DATAHDR ordering against publications.

Status: found that TXS is absent for M2/M4 and for several sequence gaps (live budget 16 not exhausted), so I'm checking what our DW5 bit 9 requests versus the selected TXS-report bits.

# Data-egress review for image b8851761 (`join-spinner.log`)

## Verdict

There is **no source-supported functional TX fix yet**. Every descriptor and routing field I could compare matches the selected gen4m command-data path: TC, port, queue, WLAN/STA selection, packet format, header format, fixed lowest rate, retry count and protection. The new evidence narrows the problem: the failing frames are exactly the ones that need the transmit-side PTK applied (PF=1). The one host frame known to have reached the AP (M2, unprotected) uses the same layout except for two bits. The new evidence does not identify a firmware or radio cause. It also cannot prove whether a lost DISCOVER or a lost OFFER is behind the missing DHCP lease. The next step should be diagnostics only.

I kept the earlier corrections: COMMAND-format protected Ethernet is a supported layout (`nic_tx.c:3394-3439`). I make no GTK-rekey claim; this saved log has no post-authorization EAPOL record.

## 1. Observed sequence and parser limits

**Sequence** (public numeric fields only):
- **Management TXS:** PID 1 and PID 2, WLAN 5, status 0, `tx_count=1`, rate 0, flags 3, sequence numbers 0 and 1 (log lines 4269-4286).
- **Security publications:** two EAPOL frames of 135 and 113 bytes (lines 4322, 4339). The WPA receipt has exactly one M1 (info 138) and one M3 (info 5066), both result 2, failure 0. No M3 retransmission was recorded, and the early bucket was not full.
- **Authorization:** at 109 705 ms (lines 4374-4379).
- **Data publications:** two IPv6 multicast frames, then 10 broadcast 342-byte DHCP DISCOVERs (`dhcp=1`, `parse=1`).
- **Received data:** group ARP, IPv4 and IPv6 only. Under `transport.c:177-180` an accepted non-EAPOL frame must have passed CCMP, key and replay checks. So group (GTK) receive decryption is proven to work; PTK receive is not proven.
- **PID 127 TXS:** 9 records, all WLAN 5, status 3, `tx_count=30`, rate 0, flags 3, TID 0, flush 0. Sequence numbers are 5, 7, 8, 6, 18, 19, 25, 26, 27 (lines 4546-4673).

**What the fields mean in the selected source:**
- `flags=3` sets both `TXS_WITH_ADVANCED_INFO` (bit 0) and `TXS_IS_EXIST` (bit 1) (`nic_cmd_event.h:2049-2052`; usage at `nic_tx.c:3533, 3552`). So sequence, count, rate and TID are authoritative (`wifi_station_data_diag.c:38-40`).
- Status 3 is `TX_RESULT_MPDU_ERROR` (`wlan_lib.h:1079-1083`). It is not `LIFE_TIMEOUT`, `RTS_ERROR`, `AGING_TIMEOUT` or `FLUSHED`.
- `tx_count=30` equals our configured retry limit (`wifi_station_transport_bind.inc:43` sets `retry_limit=30`; written to DW3 at `wifi_station_wire.c:197`). It also equals the source default `NIC_TX_DATA_DEFAULT_RETRY_COUNT_LIMIT` (`nic_tx.h:204`).

**Parser limits:**
- **PID 127 is shared**, and `last_pub` is context, not correlation (`wifi_station_data_diag.h:23-24`).
- **No TXS at all appeared for M2 or M4.** The early TXS budget is 8 and only 2 were used (`data_diag.c:60`). M2 evidently reached the AP, because M3 followed. So the firmware does not report every PID 127 frame, and successes may be under-reported.
- **Sequence-number gaps:** numbers 9-17 and 20-24 have no TXS, and the live budget was not exhausted (11 of 16 used). The data does not support "all data egress failed".
- **No DATACOUNT summary** was emitted (`summary: null`). Totals for `txs_ok`, `txs_fail` and unicast receive are therefore unknown.
- **The non-DHCP receive header budget (4) was spent** on group frames. A DHCP reply has its own budget of 12, and none was printed.
- **Receive rejects are silent.** `decode()` drops frames for RXD error bits, cipher/key/WLAN mismatch or replay with a plain `return 0` and no counter (`transport.c:147, 172-181`). `nsdd_receive` only sees accepted frames (`transport.c:190`). A unicast PTK-encrypted OFFER rejected there would be invisible.

## 2. Source discrepancies

**None proven.** The checks, item by item:

| Item | Ours | Selected source |
|---|---|---|
| TC/port/queue | queue 1 (`transport_bind.inc:43`) | TC4 maps to `PORT_INDEX_MCU`, `MCU_Q1_INDEX` (`nic_tx.c:97-104`); command data forces TC4 (`:3397`) |
| STA/WLAN for broadcast DA | AP WLAN 5, ACK expected | In infra mode the AP StaRec is used for every DA, broadcast included (`que_mgt.c:1398-1434`); BMC/NO_ACK applies only to `STA_REC_INDEX_BMCAST` (`nic_txd_v2.c:386-391`) |
| OP mode / STA type | BSS byte left 0; STA type 65 | `OP_MODE_INFRASTRUCTURE=0` (`wlan_def.h:673`); `STA_TYPE_LEGACY_AP` (`:991`) |
| Packet format | COMMAND | command data forces COMMAND (`nic_tx.c:3418`) |
| Header format | NON_802_11 + ETHERNET_II | same (`nic_txd_v2.c:413-415`) |
| Rate | DW2 bit31 on, bit30 clear (mode=DESC), DW6 rate; identical for management, which succeeded | `nic_connac2x_tx.h:628-631, 839-841` |
| Retry count | 30 | 30 |
| TXS request | DW5 bit 9 `TX_STATUS_TO_MCU` (`wire.c:198`) | `nic_connac2x_tx.h:188` |
| Key algorithm | 4 | `CIPHER_SUITE_CCMP=4` (`privacy.h:103`) |

**Data vs EAPOL in our TXD:** they differ in exactly two bits (`wire.c:193, 197`):
- **DW1 bit 10 (VTA):** set for data. The source command-data composer also sets it (`nic_txd_v2.c:404`).
- **DW3 bit 1 (PF):** set for data. The source sets it when `fgTransmitKeyExist` is true (`:426-435`).

Both mirror the source. Neither is a defect.

**Lifetime:** ours is a finite 2000 ms; source command data uses `NO_LIMIT` (`nic_tx.c:3424-3428`). This is ruled out as the cause of these failures, because the status is MPDU_ERROR, not LIFE_TIMEOUT.

## 3. Credible alternatives (none proven)

- **A. Transmit-side PTK application.** Protected frames may fail in the firmware/MAC crypto path: WTBL key state, cipher binding or PN. This would explain why only PF=1 frames fail while the same-layout PF=0 EAPOL got through. The source does not show how firmware reports such a failure, and MPDU_ERROR does not by itself mean "not on air".
- **B. Real over-air retries with no ACK matched.** The count of 30 suggests attempts, but nothing here shows actual air time or the absence of an ACK.
- **C. The DISCOVER reached the AP, but a unicast OFFER was dropped.** This would happen in our silent PTK receive validation (`transport.c:177-180`). Only group receive is proven to work.
- **D. The firmware only reports TXS for failures on this path.** In that case some data frames may have succeeded unseen. The sequence gaps and the missing M2/M4 TXS are consistent with this.
- **E. A firmware prerequisite for the command-data route on this radio.** This means the low-latency/dup-detect policy cited earlier. It remains unverified, as before.

**Weak inference:** the AP probably accepted M4. There was no M3 retransmission and no deauth or link-loss in the saved window. This does not prove PTK installation on the AP side.

## 4. Minimal correction and test

**No functional change is justified.** Do not disable PF, add a plaintext fallback, or change the ring, format, rate, key or lifetime.

Diagnostic-only additions, numeric and with no addresses or packet bytes:
1. **More TXS fields.** Log the public `EVENT_TX_DONE` fields that are already delivered but not decoded (`nic_cmd_event.h:2025-2035`): `ucRspRate`, `ucRateTableIdx`, `ucBandwidth`, `ucTxPower`, `u4TxDelay`, `u4AppliedFlag`, side by side for management successes and PID 127 failures.
   - `u4TxDelay` is the key discriminator: genuine 30-attempt air retries (alternative B) versus an immediate internal reject (A or E).
   - Decode raw TXS bits only if a selected-source decoder defines them (check what follows `nic_tx.c:3552`).
2. **Receive reject counters.** One counter per `decode()` reject branch: RXD error bits, `sec`/key-id/WLAN mismatch, replay, and short or invalid frames. Also unicast versus group acceptance counts. Emit them periodically, not only in the terminal summary. This tests alternative C.
3. **Periodic DATACOUNT.** Emit `txs_ok`/`txs_fail` totals periodically so tail TXS records aren't lost when no summary is produced.
4. **Optional, gated: unique host PIDs.** The source assigns per-frame driver PIDs to command data (`nic_tx.c:3434-3439`). Unique PIDs would allow M4 versus DHCP attribution. This touches PID-domain invariants, so it needs separate review.

**Test:** repeat the same single WPA2 join, private address off. Proceed only if the evidence separates alternatives A, B and C.

## 5. Downstream safety risks

- **Overclaiming:** never treat `tx_local`, DMA receipts, TXS presence or the count of 30 as proof of air delivery or a lease. The iOS link-local fallback shows only that DHCP did not complete.
- **Unsafe fixes:** any PF or plaintext workaround would leak data and still fail at the AP.
- **PID changes:** these must keep `transport.c:136` management air-wait matching disjoint from host PIDs.
- **Receive diagnostics:** these must not log addresses, PNs or payloads.
- **Late EAPOL delay (unmeasured):** firmware-side queuing of 30-retry data could delay a later security frame. This matters while root is waiting for any late EAPOL.
