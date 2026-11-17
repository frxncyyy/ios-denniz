# Next Wi-Fi stages — source audit, 2026-10-20 (no device actions)

Sources are local `../nord2-current-kernel`, commit04d00c25. All lines below
refer to that tree. Existing hardware success is cold CONN power/ID/restore,
not full conninfra initialization. Firmware extraction library is independent.

## Recommended minimal next hardware experiment

After memory-only extraction is independently proven on XNU, test **only two
ROM EMI payload placements and readback**, keeping WFSYS CPU reset asserted
and performing no patch/main download, CPU release, init commands or RF.
This stage is conditional on proving low-memory ownership/AP access first.

1. Resolve reserved-memory ownership from preserved DT and actual XNU memory
   map: CONN range0x8fb00000+0x450000, distinct MCIF0x8a000000. These are outside
   advertised XNU RAM0x142000000..0x180000000; that fact alone does not establish
   AP permissions, existing cache aliases, firmware ownership or safe writes.
   Map only needed spans consistently (prefer a single uncached mapping,
   no cached alias). Establish EMI MPU state without relaxing protection.
   Abort if unknown or non-cold/in-use by another subsystem. Never clear4.5MiB.
2. Extract all files to owned high RAM using fixed digest-verified loader.
   Parse/check the pinned48-byte ROM headers, target/length/alignment and
   target range before writes; full source hashes are authoritative. Existing
   ROM internal CRC fields are not yet independently validated.
3. Save original bytes for both destination payload spans and datecode spans
   before changing anything. Use owned buffers (~347KiB plus readback scratch).
   Check cold WFSYS/CPU state only after proven-safe CONN power prerequisites;
   refuse already-active WFSYS. Keep existing stage3 cleanup armed.
4. ROM WMMCU: skip48bytes, copy49820 to0x8fd30000 (offset0x230000).
   ROM Wi-Fi: skip48bytes, copy296868 to0x8fdb0000 (offset0x2b0000).
   Linux also writes16-byte header datecodes to EMI offsets0x27f0e0/0x27f0f0.
   Save/restore those spans too if reproduced. Do not copy headers or patch.
5. Complete writes with an explicit system-store barrier appropriate to the
   actual mapping, read back through the same mapping and SHA-256 compare
   expected payloads. Do not infer visibility from memcpy returning. Linux
   uses memcpy_toio; XNU mapping/cache/barrier policy needs explicit review.
   Before any later CPU release: clean cache to PoC if cached, then DSB SY;
   do not mix cached/uncached aliases. Readback is useful but cannot alone
   prove future radio-side access or EMI translation.
6. Restore saved destination/datecode bytes, barrier+verify original digest,
   unmap and free. Any failed restore prevents Linux fallback and is reported
   as failure. Success log: exact expected source/payload/readback digests,
   bytes, CPU never released, all restoration1, original stage3 restored1.
   All8 SMP/input/display must still pass. This is placement validation only.

This is smaller than ROM execution: it can leave CONN2AP remap untouched
because AP physical EMI copying does not need radio-side address translation.
If EMI MPU/ownership cannot be proven, stop at memory-only extraction.

## Real Linux protocol and WFSYS prerequisites

`conninfra/platform/consys_hw.c:311–405`, cold path order:
PMIC common power -> clock buffer -> interface mux -> CONN domain -> chipID ->
EMI remapping -> D-die config -> SPI master(next_status) -> A-die config ->
AFE software patch if callback exists -> AFE WBG calibration -> subsystem PLL
initial -> subsystem status -> voltage/low-power setup. Existing stage3 has
NOT implemented the D-die/SPI/A-die/AFE/PLL portion. Do not jump directly from
stage3 ID success into WFSYS CPU release. These steps require their own audit,
allowlists, snapshots, bounded semaphore acquisitions and restoration tests.
`mt6893_pos.c:750` copies D-die efuse into SYSRAM/disables HW SYSRAM powerdown;
`:1113` A-die setup clears SYSRAM and performs strap/GPIO/SPI/efuse sequences;
`:1283` WBG calibration changes analog controls; `:1432` PLL sets30/50us
stable times0x180030f4 mask0x7fff7fff->0x03140521 and enable mux0x18003004
mask0xcf->0x4f. These are meaningful additional hardware state, not optional
read-only diagnostics. Do not touch GPIO155 (volume-down) as TCXO.

`mt6893_pos.c:101` CONN2AP remaps:
-0x180601c4 bits19:0 <- CONN EMI base>>16 (0x8fb0).
- WF/BT/GPS peripheral remaps low20 bits <-0x01000.
- MCIF/MD shared EMI remap uses separately supplied base>>16.
Save/restore owned fields and verify before future ROM execution. Do not
program unrelated BT/GPS/modem fields merely because Linux does so globally.
`mt6893_emi.c:88` Linux MPU REGION_CONN permits AP/CONN/SCP. This is source
behavior, not evidence of inherited bootloader permissions or XNU authority.
`wlanDownloadEMISection` weak MPU helper may be overridden elsewhere.

`soc3_0.c:2150–2376` WFSYS power/ROM path (future stage, NOT proposed now):
- Wakeforce0x180601a4 bit0=1; poll0x18060184 bit5=0; safe readable CONN,
  verify real platform HW ID (our measured0x20010101, ignore stale comment).
- Assert WM CPU reset0x18000010 bit0=0.
- Enable BPLL/WPLL via conninfra_bus_clock_ctrl. mt6893.c:447 uses BUS_CONTROL
  semaphore and0x18003008 bits21/20,30/50us delays; Linux retry loop unbounded.
- WFSYS top power0x18000000 key0x5746 high16, bit7=1; poll0x180602cc bit2=1.
- Disable WF sleep protect0x18001620 bit0=0, pollbit3=0, then WFDMA sleep
  protect0x18001624 bit0=0, pollbit3=0. Opening WFDMA protection is not starting
  a DMA engine, but still changes interconnect state requiring ownership.
-0x1800e06c bit0=1; pollWFSYS version0x184b0010==0x20010000.
- Call ROM EMI download described above (two header-stripped ROMs/datecodes).
- Set AP2WF remap0x18001120=0x89040000, then aperture0x185001dc/1e0=0
  for MCU/WF entry defaults. Snapshot aperture target words/remap first.
- WFSYS semaphore reset0x18000018 bit0=0.
- Release CPU0x18000010 bit0=1. This is a distinct authorization/test boundary.
- Poll ROM code index0x184c1604==0x1d1e. Source actually delays1000us despite
  0.5ms comment and allows ~100tries; use real deadline+iteration bounds.
- conninfra_config_setup, busclock release, wakeforce release.

Power-off `soc3_0.c:2379–2540`: forcewake; mask idle0x1800f004 bit0=1;
WF protect0x18001620 bit0=1/pollbit3=1; WFDMA protect0x18001624 bit0=1/
pollbit3=1; semaphore reset bit0=0; clocksrelease; keyedtop bit7=0/
poll0x180602cc bit2=0; unmaskidle; toggle0x18001c14 bit0 1->0; release A-die
clock; forcewakeoff. Linux continues after some protect failures; XNU must
fail closed and retain rails/ownership if isolation cannot be proved.
Blind raw full-register rollback is inappropriate for keyed/reset/W1C
semantics; inverse sequence+owned masks and proof before rail removal.

## Four files are not four EMI copies

Selected `gen4m/Makefile:131–146` SOC3_0 branch forces dynamic-map downloads,
POWER_ON_DOWNLOAD_EMI_ROM_PATCH=1 and ROM_PATCH_NO_SEM_CTRL=1. Therefore the
selected path differs from generic config.h defaults. `soc3_0.c:2960–3108`:
- Two ROM blobs: ROM_EMI_HEADER48bytes skipped; target u4PatchAddr masked
  into physical CONN EMI; datecode16bytes separately.
- Patch `soc3_0_patch_wmmcu_1a_1_hdr.bin`: parse PATCH_FORMAT V1/V2, then
  selected dynamic-map path/regions, NOT an EMI copy. `:3188–3241` skips
  semaphore-control query for this config but calls
  wlanPatchDynMemMapSendComplete; fw_dl.c:1016 sends
  INIT_CMD_ID_DYN_MEM_MAP_PATCH_FINISH and waits command status. No assumption
  that copying patch makes it active, or that no ROM transport is needed.
- Main WIFI_RAM_CODE has5regions (verified download-layout.json): first3
  internal targets0x104000/0x201c00/0x404000, sizes5744/5168/29408; last2 EMI
  targets0xf031e000/0xf03c3800 sizes675456/257520. Parse all tailers with bounds,
  flags, section count and CRC before any target writes. Header/tailer are not
  executable payload. Last2 are EMI, first3 are unencrypted and use selected
  dynamic-map path. `chips/common/cmm_asic_connac2x.c:2018` writes
  AP2WF remap0x18001120=u4Addr then copies through aperture mapped to0x18500000;
  that function does not restore AXI AP2WF remap, so XNU must own/restore it.
  Generic encrypted fallback uses wlanDownloadSection config/init transport.
- Main completion/start also needs INIT_CMD_ID_WIFI_START / status handshake
  (fw_dl.c:1545ff), not only copies. Init command TX/RX transport, descriptor
  ownership, interrupts/polling and DMA buffers still need separate design.

No current proposed stage executes firmware, performs pre-calibration,
handles private NVRAM or transmits. Actual ROM execution can have autonomous
side effects; only a CPU-held placement test can claim no radio CPU ran.
