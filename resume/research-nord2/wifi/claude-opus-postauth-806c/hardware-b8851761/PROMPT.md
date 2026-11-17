Resume the SAME independent source/log review session. This is new hardware evidence for image b8851761. Keep the completed followup-command-data corrections authoritative: do NOT revive the withdrawn claim that COMMAND-format protected Ethernet is inherently invalid, and do NOT assert that a late disconnect was a GTK rekey without the actual header/failure record.

STRICT READ-ONLY SCOPE: use only Read, Grep, Glob on the saved source/log files. No shell, Write/Edit tools, installations, device/USB, network, CI, register changes, or canonical mutations. Return your complete report as final text; our wrapper saves it in this new directory. Never print SSID, MAC, credentials, keys, nonce, MIC, replay-counter bytes, IP addresses, transaction IDs or raw packet contents. Only numeric public header metadata and source path/line citations.

Read first the existing corrected report:
research/nord2/wifi/claude-opus-postauth-806c/followup-command-data/REPORT.md

EXACT immutable input:
research/nord2/wifi-ios-postauth/ios/hardware-review/join-spinner.log
414371 bytes, SHA256 73824b340385861a184ef0402459c7a35f9819434f5d3d7339b4b581f9e45533.
Adjacent parsed receipts: join-spinner-data.json, join-spinner-wpa.json, join-spinner-connection.json, join-spinner-ios.json, join-spinner-backend.json and join-spinner-pin.json. These are from the new hardware run, not synthetic fixtures. Do not read the append-only live collector; root exclusively owns it and is awaiting any late EAPOL.

New observations to corroborate and explain:
- WPA local M1/M3 cryptographic evaluation succeeds; actual PTK/GTK/replay/controlled-port and BSD link authorization were reached.
- Repeated actual native publication of 342-byte broadcast DHCP DISCOVER (public header classification). No OFFER observed by the bounded native RX diagnostic/counters; group ARP/IPv4/IPv6 are received. No DHCP lease/IP usability claim.
- Management PID1/2 TXS status0/count1/rate0.
- Shared host PID127 TXS status3/count30/rate0/flags3/WLAN5, advanced sequence observations 5,7,8,6,18,19,25,26,27. PID127 is reused for host security/data, so a TXS cannot be attributed to an individual M4 or DHCP packet solely from PID or most-recent-publication context. Both TXS flag bits are present; inspect selected source meaning before assuming why it failed.
- Prior source audits found no justified descriptor/ring change. Ordinary data uses the source-supported optional COMMAND data layout, but active-path policy/capability prerequisites and actual descriptor fields must be compared to the selected code, not guessed.

Task: focused source-grounded cause/fix review of DATA EGRESS and the observed MPDU_ERROR/no DHCP OFFER. Read only relevant current canonical shim/nord2-xnu/{wifi_station_wire.c,h,wifi_station_transport.c,h,wifi_station_transport_bind.inc,wifi_station_service_bind.inc,wifi_station_data_diag.c,h,wifi_wpa_nst.c,wifi_station_source_layout.h}, exact selected Android gen4m source under ../nord2-current-kernel (commit04d00c2565764c3922c9b4826b5acff1106e533f), and the frozen native-source oracles/reports if useful under research/nord2/wifi-ios-local-submit/ios. Current canonical HEAD may include documentation changes; this hardware runtime is successful CI39850764280 sourceaecd95ca388b6b7c6d61712ae22e8eb24ddfe3a6, reviewed exact kernel9f6f2d31e64d6ee0ce09c4f3e11c69e81f923087ceb34852db851d385286b5b5.

Prioritize concrete differences in source-selected TC/port/queue/WLAN/STA/rate/TXD/security key application or command-data prerequisite that can explain TXS count30/status3 while management count1/status0 and encrypted RX works. Compare exactly rate selection/fixed versus auto, EAPOL versus ordinary data, actual AXI/HIF path and peer configuration. If there is no source-supported functional fix yet, state that clearly and identify the minimum discriminating existing numeric fields/evidence. Do not propose random ring/format/rate/key tweaks. Treat firmware status3 as named MPDU_ERROR, not automatically a decoder diagnosis or proof of absent over-air transmission.

Wire agent independently owns native candidate/source/data audit; cap agent independently owns protocol/iOS review. You are an independent reviewer, not their approver. No broad repeated scan/discovery/registry/RE work. Report (1) exact observed sequence and bounded parser limits, (2) concrete source discrepancy if proven, (3) credible alternatives explicitly unproved, (4) minimal cohesive correction/test if justified, (5) downstream safety risks. Keep source citations precise and prose readable. Return report text without writing files.
