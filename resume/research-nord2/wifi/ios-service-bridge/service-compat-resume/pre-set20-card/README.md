# Installed CARD inventory before SET20

The executed db14928f/43fd source still returns the scan-only explicit empty
CARD inventory. Actual failed-attempt logs contain no SET20/JOIN/native station
row. Original selected iOS code caches raw13 at context+80 during bind, requires
RSN bit14 before RSN_IE assembly, and CCMP bit3 for multicast and unicast suite4.
Absent RSN/WPA bits returns -3900 before Apple80211RawSet; absent CCMP returns
-3903 or fails pairwise selection. This is a sufficient static blocker on the
selected ordinary RSN path, strongly consistent with the reported UI failure.
The exact live Apple stack/error remains unobserved; no sole-cause claim.

The four-file private proposal exposes native-owned implementation inventory via
`nord2_wifi_join_card_inventory`. The actual installed service provides only
NIWC_CCMP and NIWC_RSN_IE with six installed operation flags. It checks real NSS
service availability, admission-lock-protected failure state and installed notice
lock; it reads no MMIO and does not depend on an already connected/key-ready
state. Client GET12 preserves exact shape/capacity/interface/task checks and
passes that inventory to the unchanged raw13 codec. Output is08 40 followed by
11zero bytes. Numeric bounded CARD diagnostic confirms only these bytes/count.
No user request supplies a feature mask. Stale header text is corrected.

These are implementation capability bits, not proof a handshake has run. The
installed canonical module implements full authentication/association, PTK and
GTK installation, encrypted TX/RX, port closure and disconnect for its checked
first-attempt WPA2-PSK/CCMP profile. RSN_IE names the proven IE-builder path; it
does not claim every RSN authentication mode. Native admission still refuses
WEP/TKIP/WPA1/GCMP, SAE/required PMF, private MAC and unsupported options. No new
security type or radio operation is admitted by this proposal. Hardware join,
keys, carrier, DHCP/IP and reconnect remain unproved.

The pure codec and its explicit empty-manifest semantics are unchanged. Existing
helper tests are retained.135 focused metadata/codec checks and233 actual IOUC
checks pass Clang ASan/UBSan: real proposed client calls exact extracted native
query then real canonical codec; only NSS availability/lock context and XNU are
host doubles. Tests prove inventory can precede keys, unavailable/failure atomic
refusal, exact104bits, output/caller/shape boundaries, and empty codec remains
zero. Portable runner adds `--only card`; default all retains legacy scan, join
and lifetime suites and adds CARD. Those broad suites were not rerun here.

`evidence.py` validates97 instructions in9 bounded ranges against original same
IPSW cache bytes, original RSN_IE/WPA_IE CFStrings and groupCCMP selector table.
It uses the saved baseline source from the executed kernel, so later canonical
promotion does not erase the historical source finding. No REA provider/engine
was available; ordinary offline original-byte analysis only, no installation.

Promotion is limited to four runtime files plus the runner and three changed/new
client fixtures listed in report.json. Unchanged sibling fixtures are included
privately for portable reproduction, not required promotion. Parent owns native
compilation, canonical integration, CI/image/approval/device. No such operation
was done here. The frozen source/report files must remain immutable after review.
