# Conditional next milestone after actual FW_READY

Source-only plan, Android pin `04d00c2565764c3922c9b4826b5acff1106e533f`. No hardware, active source, device or CI changes. The native main package remains frozen. Execution depends on the current main-firmware image actually reporting FW_READY with a valid retained owner; this plan does not assume that result.

The smallest useful next milestone is one real normal NIC-capability V2 query, returning firmware/chip capabilities, permanent MAC and TX resource information. Selected `chips/soc3_0/soc3_0.c:1152` sets `isNicCapV1=FALSE`. `common/wlan_lib.c:1333–1382,6773–6918` therefore uses CID0x8a/EID0xec, followed by resource update and basic/network-address configuration. V1 capability is not the selected first query. `wlanQueryNicResourceInformation:6754` initially resets software TX resources; it is not an extra firmware query. Firmware logging immediately before capability is optional/configuration dependent (`13903–13948`), and must not be enabled against an unowned dedicated log buffer.

## Capability packet and bounded API

Use the same retained fresh driver/WFDMA owner and one-in-flight command buffer. Do not reinitialize DMA or manufacture a new readiness flag. Proposed API: `query_capabilities(owner, deadline, result)`; result contains validated known TLVs, firmware version, MAC, resource limits and the raw declared/descriptor lengths. Unknown tags are skipped only within the owned event extent.

|Absolute TX offset|CID8a query value|
|---|---|
|0..31|Source CONNAC2X MAC TXD, byte count64, packet formatCMD2/header formatCOMMAND|
|32..33|LE length32 (total minus MAC32)|
|34..35|Zero after command allocation; do not copy stale PQ comment|
|36|CID0x8a|
|37|**0xa0**, actual `CMD_PACKET_TYPE_ID`; struct comment0x20 is stale|
|38|SetQuery0|
|39|Next owned sequence|
|40..63|Source normal command extension/reserved fields, H2N index0; no payload|

Anchors: `include/nic_cmd_event.h:91,115`; `include/chips/cmm_asic_connac2x.h:465–492`; `chips/common/cmm_asic_connac2x.c:127,264–302`; `wlan_lib.c:6792–6845`. The existing generic `nwt_capture` can carry this frame; its finish-result wrapper cannot parse this response.

RX uses MAC RXD24 followed by **normal WIFI_EVENT12**, then capability V2: LE element-count16/reserved2, then repeated LE tag32/body-length32/body. EID/sequence remain at full-frame28/29, but TLVs begin at40 rather than INIT-event payload32. Validate descriptor extent, each 8-byte TLV header/body, count traversal without overflow, duplicate critical tags and minimum known-body size. Do not invent INIT length equality. Source declarations are `nic_cmd_event.h:800–813,979–1018`; parser `nic/nic_cmd_event.c:3087–3128`.

Useful tags: TX resources0, HW version5, SW version6, MAC7, PHY8 and MAC capability9. MAC body is8 bytes (MAC6+reserved2), reject zero/multicast before publication; source `nic_cmd_event.c:2593–2605`, `nic_cmd_event.h:1077`. Do not print the actual MAC in general diagnostics. Capability parsing must precede normal TX-resource accounting (`wlan_lib.c:1368–1376`). Add a bounded normal-event dispatcher: Linux skips unrelated events until a3-second capability deadline; current single-response exchange deliberately refuses them. A proper worker must retain/rearm unrelated owned events safely, count them and never mistake an asynchronous notification for command completion.

## First meaningful scan

After capability/resource/basic MAC/BSS configuration, source `wlanOnPostFirmwareReady` loads manufacturing data and regulatory policy (`wlan_lib.c:1112–1121,5272–5335`). FORCE_CAL alone does not replace these. Resolve this finite normal-operation setup before RF scan.

Scan is normal CID3/SetQuery1 with `CMD_SCAN_REQ_V2`, no synchronous command response (`mgmt/scan_fsm.c:249–465`; enum `wsys_cmd_handler_fw.h:180`). Payload size1188, full frame1252; retained buffer fits the existing4-KiB command slot. Prefix bytes0..7 are scan sequence, BSS index, type, SSID type/count, probe count, function mask and version1. Source passive type0 and specified-channel type4 are in `include/mgmt/scan.h:196–231`. Start with a bounded passive request over source-validated legal channels, after a real BSS/MAC/regulatory setup; do not invent a country or dwell configuration.

Selected struct offsets were extracted from source and checked by the local C layout helper: SSIDs8, probe delay152, dwell154, timeout156, channel type158/list160, IE length224/IE226, extended channels830, extended SSIDs896, BSSID1112/random MAC1118. Natural padding matters; do not concatenate fields as a packed struct. See `layout-evidence.c/.txt` and `wsys_cmd_handler_fw.h:1088–1159`.

Completion is unsolicited EID0x0d, with scan sequence and channel statistics (`wsys_cmd_handler_fw.h:398,1687–1721`). **It is not a BSS list.** Actual BSS results are beacon/probe-response management frames: `nic/nic_rx.c:102–132,3615–3705`, then `mgmt/scan.c:3393–3638`. Therefore implement sustained RX descriptor/group/header decoding and management-frame draining, including DMA0 data rings, before claiming a scan works. Preserve binary SSID length, BSSID, channel/frequency, RSSI/RCPI conversion, timestamp/age and bounded raw IEs. Event-ring-only polling is insufficient.

## Real iOS boundary

Existing `wifi/topaz-reference/report.md` pins Topaz89451b8 and establishes a useful hardware-neutral API: ready/address, asynchronous scan snapshots, join/leave, Ethernet TX/RX and link hooks. Its public checkout lacks the actual iOS user-client/IO80211 bridge. Its ADT wlan node and wifid startup gate are discovery prerequisites, not a replacement for a driver.

Create a dedicated radio worker and request/completion queues, then a real BSD ifnet with correct mbuf ownership, TX backpressure and link-down state until association/key installation succeeds. A valid MAC/capability reply is not carrier. Settings requires a separately verified iOS16 Wi-Fi control/service bridge for scan/join/keys; adding only an ADT wlan node or an Ethernet ifnet cannot honestly supply it. Association, key installation/EAPOL, data rings, DHCP/DNS and IP traffic remain separate concrete implementation work. No fake Wi-Fi availability should be published.

Stop here until the current FW_READY/owner outcome is known. The next executable change should be the bounded normal CID8a capability exchange and parser, not a scan or an iOS-ready assertion.
