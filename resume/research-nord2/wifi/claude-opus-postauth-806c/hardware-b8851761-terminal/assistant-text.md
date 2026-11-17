# Updated review: terminal run (`postauth-terminal.log`)

My two earlier reports stay as historical record. One statement is revised: the earlier "no OFFER observed" applied only to the early 414 371-byte prefix. In the terminal summary, the native counters classify one OFFER and one ACK among accepted **group** frames. Those are not the phone's own responses (section 2).

## 1. Why the connection closed

**Observed:**
- The closing frame is WPA record 3 (log line 6260; `postauth-terminal-wpa.json` record 3):
  - state 5 (authorized)
  - 131 bytes, declared length 127
  - key_info 4994 (`0x1382`), class 5
  - result 0, failure 4 (`NWP_F_KEY_LENGTH`, `wifi_wpa2.h:12`)
- It arrived about 357 s after authorization (109 705 ms) and was followed by the teardown:
  - `IFNET LINK authorized=0` (line 6266)
  - JOINNOTICE 3/4 with result 2 at 466 990 ms (lines 6315-6316)
  - SCANRETURN with `completed=118 tx=118` (line 6322)

**Proven from source:**
- **The frame passed the envelope check.** `wifi_wpa2.c:133` requires version 1–2, type 3, `be16(p+2)==n-4`, descriptor 2 and `be16(p+97)==n-99`. So the length fields were self-consistent, with 32 bytes of key data.
- **It then failed the key-length check, which runs before anything else.** `wifi_wpa2.c:135` is `if(be16(p+7)!=16) return failed(w,r,4);`. That check comes before the key-info dispatch (`:136`, `:148`), the MIC check (`:149-150`), unwrap and the KDE checks.
  - The check applies a fixed length of 16 to every message type.
  - The raw KeyLength value is not logged; all that is known is that it was not 16.
- **The rejection tears down the connection.** `failed()` → `fail()` wipes the engine state (`:72-73`). `stc_eapol` turns `result<0` into `stop(STC_FAILED)` (`wifi_station_transaction.c:141`).

**Inferred:** the public header fits a group-key handshake message 1:
- `0x1382` means descriptor version 2, group (pairwise bit clear), ACK, MIC, Secure and Encrypted Key Data, with no Install bit.
- 32 bytes of key data matches one wrapped GTK KDE in size.

**Unknown:** whether the frame's MIC, replay counter, unwrap or GTK contents were valid. The key-length check rejects the frame before any cryptographic step runs. The frame must not be described as authentic, accepted or installed. Group-key handshake support really is missing: even with a different key length, `:148` rejects every key_info except `0x13ca`.

## 2. Does the terminal evidence change the data-path conclusions?

**Observed** (line 6301 DATACOUNT, 6302-6305 DATATYPE/DHCPSTAT):

| | Transmit | Receive |
|---|---|---|
| Totals | 2 EAPOL, 62 data | 0 unicast, 1307 group |
| Unicast vs group | unicast 2 (EAPOL only); all 62 data frames group-addressed | all accepted frames group |
| DHCP | 30 DISCOVER; 0 REQUEST, 0 other types | 2 DISCOVER, 1 OFFER, 3 REQUEST, 1 ACK |
| TXS | `txs_ok=3`, `txs_fail=9`, `ambiguous=9` | — |

- The earlier prefix printed 9 PID 127 failures. The terminal totals contain exactly 9 failures and 9 PID 127 events. So **every PID 127 TXS in the whole run was MPDU_ERROR**, and the 3 successes all came from management PIDs.
- 64 host frames were published, but only 9 produced any TXS.

**What this establishes:**
- **Group receive decryption works.** A non-EAPOL frame is only accepted after the CCMP flag, GTK key-id, WLAN and per-lane PN checks pass (`wifi_station_transport.c:176-181`). 1307 group frames got through.
- **No own response is shown.** No transaction, client or address correlation is kept. The group OFFER and ACK can belong to other clients, so they don't show that the phone got its own OFFER/ACK or an IP.
- **The phone's DHCP client never got past DISCOVER.** It sent 30 DISCOVERs and no REQUEST, and iOS fell back to link-local.
- **Unicast PTK receive is still unproven.** `rx_uni=0` across the whole run. Unicast frames rejected in `decode()` are dropped without a counter (`:147`, `:172-180`), so there is no way to tell whether a unicast OFFER arrived and was rejected or never arrived.
- **Possible but unproven signal:** we received only 2 DISCOVERs against 30 sent. If the AP had relayed our broadcasts back to the BSS, more echoes might be expected. Without sender correlation, and without knowing whether hardware filters our own source address, this does not prove the AP missed our frames.

**Conclusion unchanged:** the terminal evidence identifies no descriptor, ring, rate, queue, format, key or lifetime defect. No functional TX change from the earlier reports should come back. PID 127 attribution is still ambiguous, and MPDU_ERROR is still not proof that nothing went over the air.

## 3. Safety obligations for the separately owned work

**Secure GTK rekey (cap agent):**
- **Replace, don't remove, the key-length check.** Make the expected key length explicit per message type; do not just delete the 16-byte check.
- **Strict validation order:**
  - Accept only the exact group message 1 key_info, and only in the authorized state.
  - Verify the KCK MIC before unwrap or any use of the key data.
  - Require the replay counter to be strictly higher than the last accepted EAPOL-Key frame.
  - Require exactly one well-formed GTK KDE with a valid key ID, and no unexpected KDEs.
- **Key lifecycle:**
  - Install into a per-key-ID group slot with its own RSC/PN replay lane, initialised from KeyRSC.
  - Keep the current GTK and its replay state until the new key is confirmed.
  - A retransmitted message with the same key must never reinstall or reset its PN.
  - The current single GTK replay slot (`nst_replay_install`, `transport.c:125`) and overwrite-style reuse of `STC_CID_GTK` are not sufficient.
- **Message 2:** send it with a MIC and with PTK protection (`nst_eapol(...,protect=1)` requires `ptk_ready`, `transport.c:62`).
- **Failure handling:** no partial install, wipe key material, and keep an explicit close/drop policy. Do not widen acceptance, and do not silently ignore unknown messages.

**Normal TX (wire agent):**
- **Keep protection mandatory.** Post-authorization data stays PF=1, with no plaintext fallback.
- **Respect cleanup gating.** Keep TX blocked after the port closes (only deauth is allowed, `transport.c:52`), and keep the key-delete → peer-retire cleanup order.
- **Claims and evidence:**
  - Never count DMA, `tx_local` or TXS presence as air delivery, a peer receipt or a lease.
  - Any per-frame TXS attribution needs host PIDs disjoint from the management air-wait match (`transport.c:136`).
- **Path changes need evidence:** any new ring, format, queue or rate choice must cite the selected gen4m path and be backed by hardware evidence. Sequence and PN stay owned by hardware.
