Status: Android BSS path keeps a real BMC (`nic.c:1958-1969`) while the STA path is the only `255` writer; now reading the 08e6 hardware receipts for the numeric RX/TX counters.

No existing fixture pins the STA-record BMC byte, so the candidate won't contradict prior suites. Checking what RXREJECT reason 2 means before citing it.

I have everything needed. Writing the candidate now: exact count-checked edits, a review diff, the real-source tests, the runner, then the report.

Now the source/layout oracle, which derives the 255 value, the unconditional assignment, and the byte offsets from the selected Android source rather than from constants.

Now the report.

I've built the private DHCP candidate. It contains one functional change: the STA-record command now sends BMC index `255`, as selected Android does. **Nothing has been compiled or run** (no execution tools); Codex needs to run it.

**The mismatch (proven from source):**
- Android always sends `255` (`WTBL_RESERVED_ENTRY`) as the STA-record BMC index (`cnm_mem.c:1141-1142`). Both STA send paths go through that one function: allocation (`:699-702`) and state change (`:973-975`).
- `255` is the value on both `privacy.h` branches (`:115-119`), and the field sits at byte 55 in both Android's layout and ours.
- Our `peer_progress` sends group slot `4` there for both state 1 and state 3 (`transport.c:305` → `wire.c:159`).

**Unchanged on purpose:** the BSS-info BMC field and the GTK slot mapping. Android keeps the real slot in BSS-info too (`nic.c:1958-1969`).

**Checked with no proven mismatch:** normal TXD/TXP and Ethernet translation, PTK/GTK WLAN selection, and broadcast-DA routing to the AP. QoS, A-MPDU and the other STA-record fields we leave at zero are listed as unproven, not changed.

**Causality is not proven.** The firmware's STA-record handling isn't visible in source, so I can't show this mismatch causes the status-3 TXS or the missing DHCP. It is credible, since the failures are specific to protected TX while the unprotected M4 and group RX work. It's simply the only source-justified functional change, so it's the one to test.

**What the candidate changes:**
- `wifi_station_wire.h`: adds `SW_WTBL_RESERVED_ENTRY 255`, with the source citation.
- `wifi_station_wire.c` (`sw_sta`): writes `255` and rejects any other value, so a stale caller fails instead of sending the wrong byte.
- `wifi_station_transport.c`: the STA initializer uses the reserved entry; the BSS initializer on the line above still uses `group_wlan`.

No guards, DMA/credit ownership, key or replay gates, cleanup, TXD/TXP layout, rates or fake ACK/key-ready paths are touched.

**Tests:**
- **`source_oracle.py`** derives the `255`, the single unconditional assignment and its two callers, the real BSS BMC, and the byte offsets by parsing the Android source. It also checks the patched copy, and has a mode that confirms the unpatched baseline.
- **`test-sta-bmc.c`** reuses the existing `transport-hardware.inc` model and checks the bytes actually published. It covers:
  - state 1 and state 3 with BMC 255 on WLAN 5, and activation still firing;
  - BSS BMC 4 on prepare, configure and retire, and GTK on slot 4;
  - protected data on WLAN 5 (TXD0, PF set, Ethernet-II);
  - M2/M4 unprotected and G2 protected;
  - retire wiping keys, and failure injection at every hardware call.
- **`run.py`** requires the test to pass on the candidate and **fail on the unpatched baseline**, so it really discriminates. With `--regressions` it also runs the existing station, normal-tx and gtk-rekey suites with the changed files overlaid.

Possible friction: since nothing was compiled, `-Werror` or harness details may need small test-side fixes. Those would be test defects, not evidence about the candidate.

**Next hardware:**
- **Success:** iOS shows a non-link-local address and a router.
- **Fix not enough:** PID127 still reports status 3 / count 30 and unicast RX stays at 0. The next real ambiguity is then the combined RX error gate (`transport.c:211`), which doesn't record which error bits fired or whether the frame was unicast. The report describes a small bounded record for that; I haven't implemented it, to keep this to one change.

Earlier session reports are untouched. Everything is in `research/nord2/wifi/claude-opus-dhcp-08e6/`:
- `REPORT.md`
- `candidate/edits.py` — authoritative; exact, count-checked edits applied to a copy, fails on baseline drift
- `candidate/candidate.diff` — hand-written review copy
- `tests/source_oracle.py`
- `tests/test-sta-bmc.c`
- `tests/run.py`

Run with: `python3 -I research/nord2/wifi/claude-opus-dhcp-08e6/tests/run.py --log-dir <new dir> --regressions`
