Private scan/initialization revision, static shell evidence

This revision reuses frozen parent manifest41989084e7276818ad14451c2cd6f6d2a095de06ed65655024e13976b00d984f without changing its files. All addresses below are unslid addresses in the exact same-IPSW IO80211 framework TEXT/data selected in that package. There is no decompiler/provider/runtime evidence. All mounted artifact reads are read-only. The current remote Topaz remains89451b8 as independently checked in the parent package; its real backend API is a useful boundary, but it does not supply the missing service implementation.

Binding, now named by exact request table

`request-names.json` extracts bounded16-byte entries from actual framework DATA_CONST:10SCAN_REQ,11SCAN_RESULT,12CARD_CAPABILITIES,43DRIVER_VERSION. Names come from their linked actual ASCII strings, not old headers. Bind uses request43 with output capacity256 and logs `Sanity test passed. Version - %s` (0x1d3ed4f7c, string0x1d3f0996a). It then reads selector1 output1 byte into context+1473. Nonzero selects the packaged userclient path in RawGet/RawSet; zero selects BSD ioctl fallback. It is not a radio-ready flag and must not be returned as a fake hardware capability. Next mandatory request12 asks13 bytes into context+80 (0x1d3ed5050–5070). The meanings/version partition of these13 bytes remain unresolved; do not fabricate a populated capabilities bitmap merely to pass binding. Later consumers test particular raw bytes/bits, including context+91 bit6, so arbitrary zero/one bytes may choose incompatible scan formats.

Exact wifid code-signature entitlements (`wifid-entitlements.json`) include both normal and exception iokit-user-client-class lists naming **IO80211APIUserClient** and `com.apple.wifi.manager-access=true`. Thus a generically named Ethernet userclient cannot be presented as an equivalent bridge. Our private refusal shell deliberately uses a different name and is not registered; future exact class naming must preserve owning-task/entitlement policy, not disable checks.

Scan request and result contract

Apple80211ScanAsync0x1d3ebe7bc invokes builder0x1d3ebb534, which zeroes5456 bytes and stores version1 at payload+0 (0x1d3ebb588–598). It builds a RawSet request10 with exactly5456 payload bytes (0x1d3ebe890–8c0). Actual small/large RawSet envelopes encode0x802869c8 at0x1d3ec35ac/0x1d3ebc0f4; GET remains0xc02869c9. Scan builder recognizes SSID/BSSID lists, flags, priority, scan type, BSS/PHY mode, dwell/rest, channels and6GHz/FILS options. A null parameter dictionary sets type3 plus two values1 at payload+52/+56 (0x1d3ebb624–634), but this is **not** a conservative regulatory policy. This revision's request decoder captures only proven version/extent/type; it cannot authorize any backend scan. Full requested channel/flag/SSID semantics must be decoded and checked against the actual MTK regulatory/normal-init receipts before accepting a real request.

Actual result getter in0x1d3ebec90–0x1d3ebee08 builds request11 with extent1164, preinitializes version1 and IE capacity1024, calls RawGet repeatedly, then conversion0x1d3ebcb38. Result representation supported by observed field loads and their immediate CF dictionary keys:

|Offset|Width|Field consumed by this exact framework|
|---:|---:|---|
|0|4|requested result version1|
|4|4|unresolved/reserved here; encoder zeroes, consumer field semantics not claimed|
|8|4|channel (CFNumber32 at0x1d3ebd0b8)|
|12|4|channel flags (0x1d3ebd0ec)|
|16|2|presence/format scalar controlling noise/SNR reads (0x1d3ebd124)|
|18/20/22|2 each|signed noise/SNR/RSSI (0x1d3ebd12c/160/194)|
|24/26|2 each|beacon interval/capabilities (0x1d3ebd1c8/204)|
|28|6|BSSID, formatted at0x1d3ebcc78 onward|
|34|1|rate count|
|36|4 each|rates, read with index*4 at0x1d3ebd288/5cc; bounded here to15 beforeSSID area|
|96/97|1/32|binary SSID length/bytes (0x1d3ebcbec/cc00)|
|132|4|age (0x1d3ebd628)|
|136|1|flags; includes FILS-related bit2 in selected capability branch; no invented full meaning|
|138/140|2/1024|IE length/bytes (0x1d3ebd304–30c)|

`scan_contract.c/h` implements a pure version1 decoder and field encoder of exactly1164 bytes. All data are caller-supplied; none are created from FW_READY/CAP/SCAN_DONE. It bounds SSID32/rates15/IE1024, checks complete IE TLVs and leaves outputs unchanged on invalid input. This adds defensive validation absent from some framework loops. Rates/channel flags remain exact numeric inputs; **conversion from MTK rate/channel metadata is not established here**. In particular, there is no hardcoded iOS band/channel flag map or regulatory default. The encoder fills only documented consumed fields and zeroes unmodeled bytes; it remains uncalled until full record compatibility (including field+4 and actual result formatting) is reviewed. It is not a live result provider.

Async/event ownership

`event_names.py` follows exact `convertApple80211EventToString`0x1d3ef0ef8 table0x1dabacb00/2736 bytes,171 entries. Actual names include event10SCAN_DONE,15SCAN_CACHE_UPDATED,16INTERNAL_SCAN_DONE,198INFRA_SCAN_ADD. Naming alone does not establish payload ABI. Event10's top-level jump table at0x1d3ef5a4e branches to0x1d3ed7ff4; that handler consumes exactly4 bytes as status, otherwise takes its fallback path. Completion is not a BSS list.

Selector5 returns1 byte used/logged as **useIOUCEventPipe** (0x1d3ed2110–21a8). When true, the framework creates an IONotificationPort/queue or runloop, invokes selector3, maps type1(options0x1001) into context+1504(data)/1512(bytes) and type2(options1) into context+1520(state), then invokes selector2 async with8 references (0x1d3ed230c/2414/2550). The callback0x1d3ee5814 ignores synthetic scalar payloads and drains the mapped ring. Therefore selector2 async notification alone is insufficient. The mapped ring uses32-bit consumer@state+0,producer@+4, DMB/acquire/recheck and CAS consumer updates; records start on4-byte boundaries. Kind80 is a wrap/padding case; kind87 is the handled event record. Fixed record header48, primary lengthLE16@24, secondary lengthLE16@26, event codeLE32@28, interface name16@32, payload starts48. Consumer copies the header, checks a stable first word/consumer, bounds and wraps payload, caps copied data, converts to kernel-event form, invokes the common handler, then rechecks lengths/first word before CAS advancement. Full producer publication/order/full-ring/drop/cancellation semantics are not recovered here and no ring producer is implemented.

The exact copied kind87 event10/primary4/secondary0 record can be safely classified by `niws_decode_pipe_scan_done` (extent52). It does not touch a live shared mapping or advance an index. A copied ordinary kernel-event form is24-byte XNU `kern_event_msg` +16-byte ifname +4-byte status (44 bytes), consumed by0x1d3ed7c18; `niws_decode_scan_done` bounds and matches exact interface/event/length. Vendor/class/subclass are not silently trusted: native must first authenticate the actual event stream/session, and the helper does not authorize delivery. `id=0` returned for the pipe form records absence of a kernel-event ID in this narrow view; it must not be emitted as a real monotonic ID. A scan transaction needs request generation/cancellation and an actual backend completion before such a record can be delivered.

Compiled refusal-only IOUserClient shell

`ControlShell.cpp` is unregistered/uninstantiated, has no provider personality/newUserClient callsite and deliberately is not IO80211APIUserClient. It checks type0, an actual copied owning-task manager-access entitlement, retains task lifetime, serializes closed/owner checks, refuses cross-task calls, refuses every selector and memory map, and closes/terminates cleanly. No external pointer, async reference, Mach port, interface or backend is used. This is concrete lifecycle scaffolding only; it cannot serve wifid or cause networking. Its standalone nonempty ARM64 object was built with recorded CI-derived C++ headers (old fuel include and forcedNORD2 removed), **not** an exact production owning-TU/link compile. Header warnings remain in the log. nm proves the real externalMethod/clientClose/clientMemory/init/entitlement references. No host mock is claimed to prove IOKit runtime races or entitlement enforcement.

Validation and next finite contracts

`python3 test.py` runs ASan/UBSan failfast pure fixtures: full result roundtrip, every1164-byte truncation, malformed lengths/IEs/version, rate/SSID bounds, atomic refusal, exact SET/request10 envelope and event10 interface/length/status classification. The C helpers compile with the recorded normal ARM64 argv using source/output substitutions only. Synthetic fixture values are not observed networks and no MAC/SSID/board/calibration bytes are printed.

Before a real bridge: recover13-byte card capability field semantics; choose/implement genuine eventpipe support or validated fallback; fully decode scan request channels/modes/SSID/policy, map actual MTK channel/rate fields to exact iOS flags; validate field+4/result version and result cache iteration/end-of-list error; implement producer ring lifecycle with owning-task cancellation; implement mandatory get/set queries/status without fake readiness; publish real controller/interface/BSD relationship only once backend normal init/regulatory/scan/association/key/data paths exist. Topaz's backend API supplies useful asynchronous boundaries, not those missing contracts. The next best static work is wifid's actual initial Get call sites plus result-cache end semantics, not activation or a fabricated ready reply.
