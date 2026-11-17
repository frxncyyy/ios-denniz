# Private main-firmware and FORCE_CAL candidate

This directory has no active XNU hook. It must never be invoked from the
completed ROM or DMA probe image. The selected source is Nord2 Android
`04d00c2565764c3922c9b4826b5acff1106e533f`.

`fw_place.c` copies the authenticated **same retained native ROM object**
`rom.blobs[1]` patch (5726 bytes) and `rom.blobs[2]` N9 (973648 bytes) using
`nmt_parse_fixed_layout`. It accepts only the seven fixed source spans:
patch RAM at `0x00100000`, patch date at `0xf027f0d0`, N9 regions at
`0x00104000`, `0x00201c00`, `0x00404000`, `0xf031e000`, `0xf03c3800`.
The first, third, fourth, and fifth destinations use literal
`AP2WF_REMAP_1` writes at `0x18001120` followed by the `0x18500000`
64-KiB dynamic aperture; the other three are offsets inside the retained
CONN `0x8fb00000+0x450000` mapping. The core retains state before uncertain
writes and refuses replay after mutation. Its bounded readback is an added
verification step; it is not claimed to be an Android step. Source:
`chips/common/cmm_asic_connac2x.c:2018-2065`,
`chips/soc3_0/soc3_0.c:234-283`,
`chips/common/fw_dl.c:2328-2371`.

`inject_cal_adt.py` is an **opt-in future-image** packager. It reads the
locally saved `research/nord2/wifi/calibration/WIFI`, checks the exact
6146-byte SHA-256 `61ac3300…c93c4f3f89f`, requires the explicit
`--enable-nord2wifimain` packager switch, and writes a *distinct* ADT with
`/chosen/nord2-wifi-calibration` mode 0600. No calibration or MAC bytes are
embedded in C/Python source. `phy_xnu.c` requires future
`nord2wifimain=1` plus existing `nord2wifirom=1` and `nord2wifidma=1`,
then checks the ADT property and the current original FDT at the loader's
`/chosen/nord2-stock-dtb` physical address. `phy_force.c` checks the saved
WIFI hash again, selected no-ConnFem FDT policy, exact NVRAM tag span
3972..4197, coant byte243=0, and builds the source 464-byte MAC32 + INIT32
+ TLV400 FORCE_CAL CID8 frame. The expected TLV400 SHA-256 is
`2ac32f8672ac11a71c09b88df389093dbce3c234ae23a861bd5ed1f72330b88c`.
The response validator accepts only the actual owned 60-byte WFDMA capture
with packet type `e000`, EID5, matching sequence, CAL event81/status0,
and a returned EMI interval inside CONN. It does not invent an INIT embedded
byte-count equality: the selected Android source ignores that field. The
transport must prove descriptor-bounded actual RX length and retain raw
counts. `phy_backup.c` then copies the returned EMI range into retained
driver memory before N9. Sources:
`chips/common/pre_cal.c:49-108,163-260,500-590`,
`include/nic_init_cmd_event.h`, `os/linux/gl_init.c`,
`connfem/connfem_api.c`.

`phy_exchange.c` binds the PHY sequence to the peer's **v2** actual
`nwt_capture` API. It requires `nwi_state.phase==5`, driver ownership,
completed patch CID40 `finish_phase==1`, and the first transport event.
It keeps the 464-byte TX and 60-byte RX buffers in a retained native object;
semantic or EMI-copy failure latches transport halt before any next command.
`phy_xnu.c` preallocates the maximum 4.5-MiB CONN backup while sleepable,
then reads only the firmware-reported bounded EMI interval from the same
retained Device CONN mapping after actual EID5/status success.

This v2 is a narrow follow-up to frozen `main-fw-native` v1. Actual canonical
`wifi_firmware.c` indexes `blobs[0]` as the 973,648-byte main N9 image,
`blobs[1]` as the 5,726-byte patch, and `blobs[2]` as the 296,916-byte ROM
image. Preparation now selects main `blobs[0]`; v1 would have refused before
hardware access. `main_halt.h` makes any later firmware or PHY native failure
terminal for the same fresh WFDMA owner, initializer and transport. After the
halt, later PHY, finish and transport calls perform no callbacks. The native
CID41 path uses the peer's retained-buffer `nwfn_finish_command(0x41)` helper.
No frozen v1 file was changed. This private v2 still requires independent
review, final dependency pinning and parent integration; no device execution.

`fw_xnu.c` is a private native writer against the peer's fresh
`nwfn_native` object. It requires the same ROM receipt, completed region18
pool receipt, initialized nine-ring WFDMA `nwi_state`, current
AP/WF/clock/driver ownership, and all three opt-in flags. The owning TU
passes its already mapped CFG and HOST Device pages to
`nfp_xnu_prepare(cfg_io,host_io)`. It maps the exact 64-KiB dynamic
aperture once and uses the retained CONN Device map for EMI spans. A prior
4-KiB ROM diagnostic mapping of the aperture exists with the same Device
attribute; this owner never accesses that old mapping. Raw callbacks permit
only the currently selected exact span; the fresh driver owner is checked
at each 4-KiB copy boundary. One-way PCCIF4 writes use the source literal
`0x10003314 = 0, 1` only after CAL backup. CID41 and source-bounded
SW_SYNC0 (`0x180600f0`, bits0/1, 500 attempts 5 ms apart) follow all five
N9 spans. Failure after the active WFDMA lifetime latches halt and retains
resources. These files compile against CI-derived ARM64 XNU headers but are
not linked into a hardware image or independently reviewed.

The final source order remains: fresh AP/WF driver lifetime and full nine-ring
WFDMA initialization; patch payload/date; real patch-finish CID40 result;
FORCE_CAL CID8 and retained EMI backup; coant branch (saved byte243=0, so
no PMIC VFE28 action); exact PCCIF-on 0x10003314 writes 0 then 1;
five N9 spans; real N9-finish CID41 result; bounded SW_SYNC0 FW_READY
bits0/1 poll. `fw_dl.c:2328-2385` pins the order. The peer's WFDMA candidate
owns full-ring initialization and actual `nwt_capture`; there is no fake
ready/permission callback here. The peer's native owner binding and source
IRQ/coherent-error checks remain prerequisites. FORCE_CAL runs RF hardware;
any image that executes it requires its own exact authorization.

Host test:

```
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -DNFA_HOST_TEST main-fw-native/test-native.c \
  main-fw-native/{fw_place,phy_force,phy_backup}.c main_fw_layout.c \
  -lcrypto -o /tmp/test-main-fw-native
/tmp/test-main-fw-native ../../calibration/WIFI \
  ../../firmware/WIFI_RAM_CODE_soc3_0_1a_1.bin \
  ../../firmware/soc3_0_patch_wmmcu_1a_1_hdr.bin \
  /tmp/nord2-live-rebuilt.dtb
```

The FDT passed to the test above can be rebuilt with
`dtc -q -I dts -O dtb -o /tmp/nord2-live-rebuilt.dtb dtb/nord2/live.dts`.
This validates parser shape against the saved DTS but is not a current-boot
FDT hash. The source files also compile with `tools/nord2-xnu-cc.py` against
the CI-derived ARM64 XNU headers.
