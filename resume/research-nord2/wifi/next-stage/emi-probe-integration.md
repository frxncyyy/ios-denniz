# Ready next-stage EMI permission probe (not integrated or hardware tested)

New isolated source/header: shim/nord2-xnu/wifi_emi_probe.{c,h}.
Dependency: existing wifi_emi_snapshot.{c,h}. No firmware loader dependency.
Public entry: int nord2_wifi_emi_start(void), strict nord2wifiemi=1 only.

Root integration proposal (do not put this on input thread): copy both helpers
and headers into the same kernel source directory, add both C translation
units to the selected BSD build list, and declare the start hook. Beside the
existing nord2_wifi_firmware_start call after UFS root setup unlock/su.ready,
call (void)nord2_wifi_emi_start(). It creates one dedicated sleepable thread,
releases its creation reference, runs the snapshot then terminates itself.
No retry. No start/default behavior without the exact new bootarg. This is
independent of root state and does not access storage or change binding.

Only8 structured SMC READ calls: regions27/31 * SA/EA/APC0/APC1. Kernel logs
start BEFORE any SMC, all rawx0..3/status values and pure range/domain decodes,
then end/status/querycount/elapsed_ticks. Every line explicitly keeps effective
permission unknown. A known unsupported result stops issuing queries; other
known errors/invalid values remain errors even if later fields complete.
No allocation, mapped EMI, raw MPU MMIO, PMIC access, CONN access or writes.
Query count is bounded, but a hung SMC CANNOT be interrupted by a software
deadline. Do not describe this as a time-bounded operation; platform acceptance
of the exact READ ABI remains unmeasured. Worker context avoids holding input
locks but cannot guarantee secure firmware will return.

Expected success measurement:8 issued, allfield statusesNWES_VALUE, region27
SA4fb0/EA4ff4 if Linux-style CONN range is inherited, region31SA0/EA1ffff if
fullDRAM policy is inherited. Different valid values are observations, not an
excuse to change MPU. Eight queries alone do NOT resolve overlapping other
regions/priority, ownership or permission to place firmware.
All8 SMP/input/display must remain healthy. Unsupported/invalid monitor result
is a diagnostic failure, not a reason to retry with raw MPU reads or SET.

Local verification completed:
python3 tools/test-nord2-wifi-emi-probe.py
(exact bridge+existingcollector ASan/UBSan, everyerrorposition, strictgate,
threadstartfailure/reference cleanup/selftermination; mocktransport,noSMC).
python3 tools/nord2-xnu-cc.py research/ci-39398655023/buildtree bsd \
  shim/nord2-xnu/wifi_emi_probe.c /tmp/nord2-wifi-emi-probe.o
(native ARM64 BSD compile; no device execution).

# ROM placement prerequisites versus common initialization

Permission snapshot does not need CONN powered or common conninfra init: it
asks the monitor for MPU metadata using the selected Android structured READ.
It therefore must NOT trigger PMIC/power/clock/AFE/SPI initialization merely
for this snapshot.

CPU-held AP physical EMI placement also does not intrinsically require full
common init or radio CONN2AP remapping. It still requires proof of reservation/
noaliases/currentAPaccess, no third overlapping MPU restriction, savedoriginal
bytes, digestreadback/restore, and positive current WFSYS cold/CPUreset state.
Current cold CONN power/ID restoration success is not that last proof. A later
conditional WFSYS read-only snapshot must be added within the existing guarded
power probe ONLY after ack/readability/identity, with exact allowlisted reads
0x18000010 bit0,0x18000000 bit7,0x180602cc bit2. Do not read while CONN off.
This worker deliberately does not perform those conditional reads independently.

Full D-die/SPI/A-die/AFE/PLL common initialization IS required before attempting
WFSYS execution per consys_hw.c311ff. It changesSYSRAM/strapGPIO/SPIefuse/
analogcalibration/PLLcontrols. It must have separately auditedownership,
boundedsemaphores, snapshots and restoration; no minimal safe port follows
from MPU metadata alone. Selected connsys_a_die_cfg_mt6893 clearsSYSRAM and
connsys_afe_wbg_cal changesanalogcontrols, so do not hide these behind a ROM
placement helper. No CPU release or RF is authorized/implemented here.
