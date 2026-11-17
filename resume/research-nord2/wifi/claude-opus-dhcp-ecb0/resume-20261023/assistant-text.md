The two `UC2ME` records are the only unicast-to-me frames observed, and both carry OFLD=3. Checking the selected source's OFLD definition and how its receive path routes OFLD frames.

Key finding is clear. Let me read the native-regression harness to author a faithful (real-`decode()`) test rather than a constant mirror.

The decisive source divergence is confirmed. Writing the candidate, test, runner, and report.

Now the authored report.

Done. Read-only analysis of the new immutable `pause-final` evidence, plus an authored (unexecuted) candidate, all inside `resume-20261023/`.

## Finding

One concrete, source-backed divergence sits exactly where the only unicast-to-me frames died.

- **Native RX drop gate** (`shim/nord2-xnu/wifi_station_transport.c:211`) rejects any frame with nonzero **OFLD** (RXD DW4 bits 11-12) before the security/header checks, via `d4 & (3u<<11)`.
- **Selected Android** never drops on OFLD: `nic/nic_rx.c:3661-3688` routes OFLD/translated frames to the data flow ("1:EAPOL 2:ARP/NS 3:TDLS"), and `nic_rxd_v2.c:363-514` drops only on FCS/DAF/ICV/TKIP-MIC.
- Both observed UC2ME CCMP records (`d4=6144`=OFLD3, `d1=d2=0`, PF=0) were rejected here; `RXCOUNTS unicast=0` throughout and the later key/replay/peer rejects are 0, so these were the only unicast-to-me frames in the capture.
- The third error record is correctly a different, real condition: DW2 bit25 `TRANS_FAIL` on a broadcast — not DW1 ICV, not the unicast path.

## Proven vs. hypothesis

- **Proven:** the gate is stricter than the selected source, and it fired on the only unicast-to-me frames.
- **Not claimed:** that those frames are the DHCP reply. The source labels OFLD=3 as **TDLS**, EtherType/payload were never captured, and shared PID127 TX status stays unattributable. So this is explicitly hypothesis-level, not a DHCP-fix promise. Unprotected M2/M4 success and group RX still don't prove a working PTK-encrypted unicast path.

## Candidate (private, security-preserving)

`candidate/edits.py` — one exact, count-checked edit on a copy of canonical: removes only the OFLD term from the hard-drop, keeps the numeric RXERROR breadcrumb, and lets the frame fall through to the **unchanged** CCMP/key/replay/peer/port/length checks (invariants asserted). All real error drops (cipher/ICV/FCS, TRANS_FAIL/EXCEED/DE_AMSDU/FRAG, pattern-filter) are untouched.

I also flagged the lower-risk alternative: add the translated-frame EtherType (one public numeric field) to the OFLD breadcrumb so the next capture itself distinguishes "OFLD gate eating DHCP" from "OFLD=3 unrelated TDLS."

## Tests (authored, unexecuted)

`tests/test-ofld-rx.c` drives the real `decode()` via the canonical hardware model: clean unicast delivered on both builds; ICV error dropped on both; OFLD=3 dropped on baseline but routed-and-delivered on candidate; OFLD=3 with a bad key still rejected by the key gate. `tests/run.py` requires that exact discrimination with no sanitizer output, plus optional station/normal-tx regressions.

## Parent run requirement

`python3 -I research/nord2/wifi/claude-opus-dhcp-ecb0/resume-20261023/tests/run.py --log-dir <new dir> --regressions` — confirm `baseline_drops_ofld=true`, `candidate_routes_ofld=true`, no sanitizer failures, regressions pass. Compile friction is possible since nothing was built; that would be a test defect, not evidence. No device/build/approval performed — those remain yours.

Files: `resume-20261023/REPORT-AUTHORED.md`, `candidate/edits.py`, `tests/test-ofld-rx.c`, `tests/run.py`.
