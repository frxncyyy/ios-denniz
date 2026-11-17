Reading the canonical client/join/station sources now; a line count comes first.

Reviewing the SET20 RSN-IE producer evidence against the strict 22-byte decoder check.

Tracing the RSN-IE builder in the original disassembly (bl at 1d3ed65d0, caps construction).

Checking why some scans end `done=1 partial=1`; the station prepare step refuses to start a join after one.

Reading the transaction/owner state machine next; any `nst` fail() during a join turns into a kernel panic through `njs_fatal_owner`, so I'm checking which ordinary timeouts can reach it.

Now checking the BSD ifnet/carrier/BPF path that DHCP will depend on.

Found a likely DHCP blocker: the link-on kernel event payload is placed 4 bytes past where XNU reads it, and the host double hides this. Verifying the registry (lladdr, BPF attach) next.

Checking how Android gen4m maps the RX WLAN index for group-addressed frames (DHCP offers can be broadcast), since `decode()` requires `wlan==group_wlan` for them.

Still going: verifying how the framework picks the AKM for an AP that offers several (PSK/PSK-SHA256/SAE), since the decoder accepts only AKM 2.

Checking one more pre-SET20 CARD gate at `1d3ebfee8` (a -3903 refusal).

# Nord2 iOS16 first WPA2-Personal connection: static review before the next kernel build

**Scope.** This was a read-only static review of the canonical candidate sources, the same-IPSW producer disassembly and the selected Android gen4m sources. I ran no commands, modified no files and took no device, CI or build action. A static review cannot rule out hardware problems.

## 0. The GET12 CARD fix is complete for this function's gates

I re-traced the selected framework association function against the new CARD reply (`08 40 00…`: raw bits 3 and 14 only). Every CARD-dependent branch before SET20 now takes the WPA2-PSK/CCMP path:

| Producer address | CARD bit | Result with the new CARD |
|---|---|---|
| `1d3ebfed4` / `1d3ec0df0` | 14 (WPA2/RSN) | RSN path taken; the -3900/-3903 refusals are avoided |
| `1d3ec0fd0` (group), `1d3ec10e4` (pairwise) | 3 (CCMP) | CCMP suites are selected |
| `1d3ebffb4` | 75 (clear) | PMK path: cipher 6, key length 32, `native_flags \|= 4`. The decoder accepts flags 0 and 4. |
| `1d3ec1160` → `1d3ec137c` | 52 and 75 (clear) | RSN capabilities = 0, so MFPC=0 |
| `1d3ec14e4..f0` | 46 (clear) | Synchronous GET21 poll at `1d3ed66f0`: 400 × 20 ms = **8 s budget**, then SET22 at `1d3ec1504` |

- The SET20 `value` field is zeroed by `stp q0,q0,[x19,#256]` at `1d3ebf280` (x19 = sp+240, so sp+496..527 are cleared). The client's `value==0` check is therefore safe.
- **Consequence of the 8 s poll:** authentication, association, the 4-way handshake, PTK/GTK install and the ifnet link-up must all finish within 8 s of SET20. If they don't, the framework disassociates.

The remaining risks to SET20 itself are findings 3 and U1 below.

## 1. Definite code defects (blockers first)

### 1. KEV_DL_LINK_ON/OFF payload sits 4 bytes past where XNU reads it, so configd probably never sees link-on and DHCP may never start
- **Where:** `wifi_ifnet_link.inc:52-67`, `nwil_event()`.
- **What's wrong:** `data=(link_data*)(header+1)` places the payload at `sizeof(struct kern_event_msg)`, which is 28 bytes because of `event_data[1]`. `ifnet_event()` posts from `&event->event_data[0]` (offset 24, `KEV_MSG_HEADER_SIZE`) with length `total_size-24`.
- **What configd sees:** for en0 it reads `if_family=0`, `if_unit=6` (the Ethernet family value) and `if_name=""`. It looks up an interface called "6". The link-on event is lost even though IFF_RUNNING is set correctly.
- **Why the tests pass:** the host double hides this. `tools/fixtures/nord2-wifi-ios-registry/test-registry-xnu.h:63` defines `kern_event_msg` without `event_data`, and line 66 reinterprets the whole buffer.
- **Fix:**
```cpp
static int nwil_event(ifnet_t net,bool up) {
 /* ifnet_event() posts event_data[0..total_size-KEV_MSG_HEADER_SIZE); consumers
  * read struct net_event_data {family,unit,name} from exactly there. */
 struct link_data { uint32_t family,unit; char name[IFNAMSIZ]; };
 alignas(struct kern_event_msg) uint8_t bytes[KEV_MSG_HEADER_SIZE+sizeof(link_data)]={};
 static_assert(sizeof(bytes)>=sizeof(struct kern_event_msg),"event header");
 auto *header=(struct kern_event_msg *)(void *)bytes;
 auto *data=(link_data *)(void *)(bytes+KEV_MSG_HEADER_SIZE);
 header->total_size=sizeof(bytes);
 /* vendor/class/subclass/event_code/data fill unchanged */
```
- **Regression test:** give the fixture the real layout (`uint32_t event_data[1]` and `#define KEV_MSG_HEADER_SIZE offsetof(kern_event_msg,event_data)`). Make the fake `ifnet_event` decode `net_event_data` from `event_data`, then assert family is Ethernet, unit matches and name is `"en"`.

### 2. An M1 that carries a PMKID KDE is rejected, so the join fails right after association
- **Where:** `wifi_wpa2.c:114`: `if(!nonzero(p+17,32)||be16(p+97))return fail(w,r);`
- **Trigger:** many consumer routers include a PMKID KDE (`dd 14 00 0f ac 04` + 16 bytes) in M1 for WPA2-PSK.
- **Result:** `nwp_cancel` → `stop(STC_FAILED)` → result 2 → error -3906 ("unable to join"). This happens on the only attempt of the boot.
- **Fix:** validate and ignore it. No PMKSA caching is added.
```c
/* M1 key data is plaintext and may hold one PMKID KDE; no PMKSA caching. */
static int m1_data(const uint8_t*p,unsigned n){
 unsigned pmkid=0;
 for(unsigned at=0;at<n;){
  if(p[at]==0xdd&&(at+1==n||p[at+1]==0)){for(unsigned j=at+1;j<n;j++)if(p[j])return -1;return 0;}
  if(n-at<22||pmkid++||p[at]!=0xdd||p[at+1]!=20||p[at+2]||p[at+3]!=15||p[at+4]!=172||p[at+5]!=4)return -1;
  at+=22;
 }
 return 0;
}
/* in nwp_receive, M1 branch: */
 if(!nonzero(p+17,32)||m1_data(p+99,be16(p+97)))return fail(w,r);
```
- **Regression test:** M1 with a PMKID gives the same M2 bytes and PTK as M1 without one. Truncated, duplicated, wrong-OUI or nonzero-padding variants are refused. A retransmitted M1 with a PMKID is still answered.

### 3. The framework can send a 24- or 28-byte station RSN IE, which the decoder refuses (mixed WPA2/WPA3 with optional PMF)
- **Producer evidence:** after writing the caps at `1d3ec138c`, it looks up `IE_KEY_RSN_BCIPHER` (key `0x1dabae998`) in the AP's RSN dictionary at `1d3ec1398`. If the value is present and nonzero:
  - it writes PMKID count 0 (`1d3ec13c4`);
  - for BIP 6, 11, 12 or 13 it appends `00 0f ac xx` (`1d3ec13ec`, `1d3ec1570..1594`);
  - otherwise it logs and keeps the 24-byte form (`1d3ec1548`);
  - the length is computed at `1d3ec15a0..15c0`.
- Transition-mode APs typically advertise a group-management cipher.
- **Where it fails:** `wifi_ios_join.c:18` requires length 22 → `NIWJ_SECURITY` → `kIOReturnUnsupported`. No JOIN attempt reaches the radio.
- **Fix:** accept exactly these producer forms and normalise them to the 22-byte IE. Native association and M2 already both use the fixed 22-byte IE, so the frames stay consistent. With MFPC=0 the trailer means nothing.
```c
/* Producer 1d3ec138c..15c0: caps0 (CARD bits52/75 clear); only when the AP
 * dictionary has IE_KEY_RSN_BCIPHER: PMKID count0 plus BIP6/11/12/13 suite. */
static int station_rsn_ok(const uint8_t*p,unsigned n){
 if(n==22)return same(p,station_rsn,22);
 if((n!=24&&n!=28)||p[0]!=48||p[1]!=n-2||!same(p+2,station_rsn+2,20)||p[22]||p[23])return 0;
 return n==24||(!p[24]&&p[25]==15&&p[26]==172&&(p[27]==6||p[27]==11||p[27]==12||p[27]==13));
}
/* niwj_decode: */
 if(!station_rsn_ok(p+206,j16(p+204)))return refuse(d,NIWJ_SECURITY);
```
- **Also do:** add `mode`, `auth_lower`, `auth_upper` and `rsn_bytes` (numeric only) to `niwj_diagnostic` and to the JOINREQ printf. Today a refused SET20 can't be explained from the log.
- **Regression test:** decoder accepts the 24 and 28 (BIP-CMAC) forms with output identical to the 22-byte form. It refuses caps 0x80, a bad OUI/type, a length byte that doesn't match `j16(p+204)`, and a 26-byte IE. Poisoning the inactive range from byte 234 onward still passes.

### 4. M3 key data containing an RSNXE or a vendor KDE is rejected
- **Where:** `wifi_wpa2.c:86-104`, `kde()`. Anything other than one RSN IE (48) and one GTK KDE returns -1.
- **Trigger:** WPA3-transition APs with SAE hash-to-element put an RSNXE (element 244) in the Beacon and repeat it in M3 (802.11-2020 §12.7.6.4).
- **Result:** M3 fails, no M4 is sent, and the join fails.
- **Smallest fix:**
```c
 unsigned at=0,have_rsn=0,have_gtk=0,have_rsnxe=0;
 ...
  if(e[0]==48){/* unchanged */}
  else if(e[0]==244){if(have_rsnxe++||len<3)return -1;} /* RSNXE: SAE-only bits, no PSK effect */
  else if(e[0]==221&&len>=6&&!(e[2]==0&&e[3]==15&&e[4]==172)){} /* unknown vendor KDE: ignore */
  else if(e[0]==221){/* existing GTK KDE checks */}
  else return -1;
```
- **Optional, stricter:** copy the Beacon's RSNXE in `niwj_select` and require M3 equality.
- **Regression test:** M3 with RSNXE + RSN + GTK installs keys. Two RSNXEs, or an unknown `00-0f-ac` KDE, are still refused.

### 5. The join is refused when the latest scan merely overflowed 16 results
- **Where:** `wifi_station_transport_bind.inc:18` and `wifi_station_transport.c:19` both refuse `rx.scan.partial`.
- **Cause:** `wifi_scan_bss.c:89` sets `partial=1` on result overflow (`NSB_RESULTS=16`), with `done=1` and state 7.
- **Seen in the hardware trace:** tickets 38, 45 and 48 show `done=1 partial=1 bss=16`.
- **Result:** if the scan just before SET20 overflowed, `nst_native_prepare` returns -1 → early JOIN_FAILED with no radio activity. The epoch is not consumed, but this attempt fails.
- **Fix:** in both places, replace `partial` with `(partial&&!dropped)`. A timeout, a non-7 state or a non-done scan are still refused by their own checks.
- **Regression test:** prepare succeeds after a scan with `dropped=1,partial=1,done=1,state=7`, and is refused after a timed-out scan or one with state ≠ 7.
- **Optional:** let `njs_cache_scan_locked`/`niwj_select` use overflow-only scans, since their records are intact.

### 6. A SET20 that arrives while any scan is queued or running fails immediately with EBUSY
- **Where:** `wifi_station_service_bind.inc:56-58` returns 2, which the client maps to errno 16.
- **Producer behaviour:** `1d3ec14c8` → `1d3ec15dc` turns a RawSet error straight into failure. Unlike scans, I found no retry for this path.
- **Trigger:** any background or other-client scan in flight when the user enters the password.
- **Fix:** admit the join and defer it in the worker. Scan enqueue is already blocked by `njs_scan_blocked_locked` once the join is queued.
```c
/* enqueue: drop the NSS_QUEUED/RUNNING busy loop. njs_owner_work(): */
 IOLockLock(nss_native.lock);unsigned queued=njs_native.queued,active=njs_native.active,scans=0;
 if(queued&&!active)for(unsigned k=0;k<NSS_SLOTS;k++)if(nss_native.slot[k].state==NSS_QUEUED||nss_native.slot[k].state==NSS_RUNNING)scans=1;
 IOLockUnlock(nss_native.lock);
 if(!queued&&!active)return 0;
 if(scans)return 0; /* nss_owner_loop drains the already-admitted scan first */
```
- **Note:** the deferral uses part of the 8 s budget.
- **Regression test:** with a queued scan, enqueue returns 0, the scan completes before prepare, there is no EBUSY, and cancelling while deferred still produces exactly one RETIRED.

### 7. Low priority: APs advertising Extended Key ID are refused unnecessarily
- **Where:** `wifi_ios_join.c:40` and `wifi_wpa2.c:43` refuse RSN caps bit 13.
- **Why it's wrong:** that bit means the AP *supports* Extended Key ID. A STA that doesn't set it simply uses key ID 0, which the RX path already requires.
- **Fix:** drop `8192u` from both checks. Keep the MFPR and NoPairwise refusals.

## 2. Probable hardware/source mismatches

### 8. Group-addressed RX probably carries the AP's WLAN index, so broadcast traffic would be dropped
- **Where:** `wifi_station_transport.c:135` requires `wlan==group_wlan` for multicast.
- **Why it's probably wrong:** on connac2 the RX WLAN index comes from the transmitter-address lookup, which would be the AP's entry (5). Android gen4m re-derives the STA from the transmitter address (`que_mgt.c:3124-3166`), and the Linux connac2 driver maps the station's index to the group entry for non-unicast frames.
- **Result:** broadcast/multicast frames (ARP requests, broadcast DHCP OFFER/ACK, IPv6 RA) are silently dropped. DHCP or later ARP-dependent traffic fails.
- **Fix** (the transmitter address is already checked against the AP via group 4):
```c
  if(!r->installed||key!=r->key_id||(wlan!=s->peer.wlan&&!((m[0]&1)&&wlan==s->peer.group_wlan)))return 0;
```
- **Regression test:** a broadcast CCMP frame with WLAN 5 and the GTK key ID is delivered; one with WLAN 4 is also delivered; a unicast frame with WLAN 4 is refused; PN replay is still rejected.

### 9. An open-system authentication response with trailing elements is ignored
- **Where:** `wifi_station_transaction.c:113` requires `n==30`. Vendor elements after the auth fields are legal.
- **Result:** the response is ignored until the channel grant expires (deadline ≈ 4 s − 10 ms via `wifi_wpa_nst.c:63`) → TIMEOUT.
- **Fix:**
```c
 if(auth){if(n<30||le16(p+24)!=0||le16(p+26)!=2)return 0;
  for(size_t i=30;i<n;){if(n-i<2||p[i+1]>n-i-2)return 0;i+=2+p[i+1];}
  if(le16(p+28))return stop(s,STC_FAILED);}
```

### 10. Auth/assoc requests are never retransmitted
- **Android reference:** waits 512 TU for a response and retries (`aa_fsm.h:82-105`) within the 4000 ms join grant (`ais_fsm.h:103`).
- **Here:** a single lost auth or assoc frame ends the only attempt, as an orderly timeout rather than a panic (the grant deadline triggers `stop` before `nst` fail(4)).
- **Smallest fix:** add `response_deadline` and `retries` to `struct stc`.
  - Set the deadline to `last_ms+524` when entering AUTH_RX or ASSOC_RX.
  - In `stc_tick`, while the phase is still AUTH_RX/ASSOC_RX and `retries<2`, re-emit TX_AUTH or TX_ASSOC (rebuilt with `sw_auth`/`sw_assoc`). Otherwise leave it to the existing deadline.
- **Regression test:** first response dropped → the second TX succeeds; three drops → TIMEOUT followed by orderly cleanup.

### 11. Untranslated RX data frames are dropped
- **Where:** `decode()` only accepts header-translated data. Android handles both translated and untranslated frames (`que_mgt.c:3088` versus `3222-3370`), and nothing here configures RX header translation for the new station.
- **Result:** if the firmware doesn't translate EAPOL, M1 is never seen → timeout.
- **Minimum:** add a counter for untranslated data frames to the STATION line.
- **Better:** accept an untranslated, unprotected, non-QoS FromDS frame whose LLC/SNAP header is EAPOL. Rebuild the Ethernet header in place in `rx_frame` (copy DA = A1 and SA = A3 through temporaries to bytes 18..29, giving `body = m+18`). Then require the EAPOL length to equal `len-36`, `wlan==peer.wlan`, A2 = AP and A1 = self.

## 3. Unknowns that need hardware (diagnostics only, no speculative change)

- **U1 – AKM choice when the AP offers PSK-SHA256.** The producer's AKM priority loop (`1d3ec06d8..07c0`) maps AKM 6 to auth_upper 1024. OpenWrt adds PSK-SHA256 whenever 802.11w is optional. I couldn't resolve the jump-table gate statically. If AKM 6 is chosen, the decoder refuses with `NIWJ_PROFILE`. The new JOINREQ fields from finding 3 will show this.
- **U2 – Data TX route.** Data frames go through the MCU queue 1 / command-format route with the protected flag (`transport_bind.inc:43`, `wire.c:190-197`), not gen4m's data traffic classes. The first DHCP TX's `tx_ok`/`tx_fail` counters will show whether this works.
- **U3 – Legacy association.** Some APs may refuse a legacy-only, non-WMM association.
- **U4 – Grant interval.** The firmware's actual grant interval versus the 4 s request.
- **U5 – Overall timing.** Whether SET20 to JOINED fits in 8 s. JOINCONTROL and JOINNOTICE already log `t_ms`.
- **U6 – Strict rate handling.** `candidate()` refuses an AP that marks the SAE-H2E-only selector (0xFB) as basic. `nst_peer_begin` CONFIGURE refuses association-response rates outside the legacy table. This is acceptable for the narrow test, but log it if seen.

## 4. Expected first-association path and log checkpoints

1. **SET20.** Expect a JOINCONTROL line with status 0 (findings 3 and 6 are the risks here).
2. **Prepare.** The worker prepares the station (finding 5 is the risk).
3. **Peer setup.** BSS/RLM/STA_REC commands, then the channel request.
4. **Grant.** EID 0x10 grant event, which caps the deadline at the grant interval.
5. **Auth/assoc.** Open-system auth, then the association request with the fixed 22-byte RSN IE (findings 9 and 10 are the risks).
6. **Configure.** CONFIGURE, station activation (EID 0x0c) and channel abort.
7. **4-way handshake.** M1 → M2 (finding 2), M3 → M4 (finding 4).
8. **Keys.** PTK command, EID 0x24 key-ready, GTK command with replay state, then the data path.
9. **Link up.** `IFNET LINK authorized=1`, then the KEV link-on event (finding 1), the JOINED notice, event 9, and GET21 = 1 within 8 s.
10. **DHCP.** Over BPF: TX through `link_send` (U2), RX through the PN-checked path (findings 8 and 11).

Success should only be claimed from an IP address and real traffic.

**Suggested order:** findings 1–6 and 8 in one build (small, local, each with a fixture), plus the diagnostics from finding 3 and finding 11. Findings 9 and 10 if time allows.
