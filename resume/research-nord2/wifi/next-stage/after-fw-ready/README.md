# Pure post-FW_READY capability helpers

`capability.c/h` construct the exact selected normal64-byte CID8a frame and parse an already-owned EIDec event. No MMIO, DMA, SMC, memory mapping, live MAC, interface publication or hardware calls occur. Runtime binding remains conditional on actual FW_READY and a real normal-event dispatcher.

The parser bounds all element headers/body lengths by the supplied descriptor extent, rejects duplicate known tags and short known bodies, captures the two declared count fields, and skips bounded unknown tags. It decodes source HW/SW/MAC/PHY/MAC-capability tags and legacy16-byte or version1/28-byte TX resources. Unknown resource versions are reported unsupported, without interpreting them. Invalid zero/multicast MAC is marked invalid, never replaced. It accepts bounded trailing bytes and does not invent an embedded-length equality. Output is unchanged on any parse failure.

Source anchors: `nic_cmd_event.h:91,979–1118,1224–1276`; `nic_cmd_event.c:2446–2469,2593–2605,3023–3079,3087–3128`; `cmm_asic_connac2x.h:465–492`; `cmm_asic_connac2x.c:127,264–302`; `wlan_lib.c:6773–6918`. The actual macro is0xa0; the0x20 struct comment is stale. MAC32 + normal command32 is the selected query frame. Normal event body begins at full-frame36, capability elements at40; INIT event body offsets are different.

Host ASan/UBSan tests cover256 exact sequence frames, every one of183 fixture truncations, atomic output, malformed count/length arithmetic, wrong EID/sequence, duplicate known tags, invalid MAC flags, legacy/V1/unknown resources, and bounded trailing bytes. Fixtures are synthetic and no MAC is printed. Freestanding ARM64 compile passes. Scan layout evidence additionally confirms source natural padding and1188-byte payload on host and ARM64.

`PLAN.md` covers the first real scan and iOS interface boundary. `DISPATCHER.md` separates unresolved runtime event/queue ownership from these completed pure helpers. Existing frozen main-firmware packages are unchanged.
