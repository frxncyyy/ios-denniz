# Complete selected-Android I2C8 parity audit

Scope: canonical f777b2a transport/native binding, selected Android04d00c2 i2c-mtk.c/.h, exact live.dts and actual62a89dcc log. No device, CI, canonical or buildtree edits. Source/log hashes and all69 actual words retained beside this report.

## Findings that require resolution together

1. **Confirmed cleanup defect, reproduced:** normal success clears own completionIRQ1 but does not verify it cleared. A dropped/sticky W1C returns clean and restores inherited INTR_MASK over pendingIRQ1. NACK and prior-completion adoption already verify this condition. Private patch adds the same readback before FIFO cleanup/config restore. A failed read or nonzeroIRQ is sticky uncertain; no inherited mask restore or next transfer. This is a demonstrated software fault-handling defect, not a claim that normal hardware W1C is broken.
2. **Source-order gap, now closed privately:** previous port programs CONTROL/TIMING before CLOCK_DIV/HW_TIMEOUT and MCU_INTR before remaining configuration and FIFO data. No hardware failure has proved that order invalid, but tests only assumed it did not matter. Private patch uses Android setup order, unchanged values/write footprint, with MCU_INTR immediately before orderedSTART. Android c963-1203 supplies exact ordering; trace regression checks the order independently of transport model expectations.
3. **Interrupt polling parity unresolved by selected source:** Android enables AP mask0x129 during transfer (c1067-1081), while the port uses0. The IRQ handler's mask-then-read sequence does not prove status latches when masked, since the event already occurred while enabled. Coreboot/U-Boot polling sources collected by parent also enable status masks. Historical MediaTek MT6582 polling precedent disables corresponding masks ([primary Google source](https://android.googlesource.com/kernel/mediatek/+/android-mediatek-sprout-3.4-kitkat-mr2/drivers/misc/mediatek/i2c/mt6582/i2c.c)), but it is not proof for MT6893 I2Cv2. Parent is resolving explicit XNU/GIC line ownership before using source0x129; do not blindly enable an unhandled SPI or claim mask0 hardware-proven. This private patch deliberately leaves that decision untouched.

Private deliverables: audio_i2c8.c.patch, unchanged-header source overlay, test-order.c, exact extracted android-speed.inc + minimal speed-stubs.h, test-speed.c and verify.py. Root owns integration.

## Exact DTS and extension applicability

live.dts8644-8655 common node: ver2, cnt_constraint1, set_ltiming1, set_dt_div1, DMA support3/version1, IDVFS1; FIFO width defaults8bit because property absent/static zero; set_aed/control_irq_sel default0; ext_time_config1801 applies only above100kHz below/equal1MHz.

I2C8 node9466-9488: controller11d04000, APDMA11000780, main+dma clocks, divider5, GPIO122/123, APoffset100, CCUoffset200, default400kHz, open-drain. There is no controller have-DCM/have-PMIC/push-pull/APPМ/GPUPM/HS-only/hardware-trigger property. AED1a exists but common set_aed0 means Android does not program it. IDVFS common1 alone does not select packed-length path: it also requires appm, absent here.

**Intentional difference:** port uses100kHz, not DT400kHz. This follows the source's supported standard-mode branch (EXT8001, HS0), rather than copying400k EXT1801/duty45 timing. No reason was found that ordinary TFA register I2C requires400kHz. Slower rate is not proof of electrical success, and clock stretching remains physical.

## Timing and actual clock proof

Actual snapshot CLK_CFG10=05050201 selects index2, univpll_d5_d4; source clk-mt6885.c1102-1106 and1886-1890. UNIVPLL CON0ff8002e1, CON100180000, CON380000001 gives powered/enabled/reset-released2.496GHz, divided20=124.8MHz. With DTdiv5: hclk24.96MHz. Native mapping retains the same shared windows; no alias mapping or clock retune introduced.

Compiled exact Android mtk_i2c_calculate_speed+i2c_set_speed functions, not a hand-translated model, independently match port setup for all supported selected-parent examples:

| Parent | TIMING including TIMEOUT_EN | LTIMING | HS | HW_TIMEOUT |
|---|---|---|---|---|
|26MHz|001b|001a|0|386|
|68.25MHz mainpll-derived|0217|0096|0|396|
|124.8MHz actual univpll-derived|0419|0118|0|400|

Source timing c465-619, timeout c963-977, divider c978-985. The sample/step decrement quirk with cnt_constraint1, integer division and timeout ceil agree exactly. No alternate PLL formula, divisor or timing bug found for actual captured mux state.

## All registers, widths, normal ordering

Source c40-88 uses byte DATA_PORT and16bit controller registers with V2 channel-relative mapping; APDMA is32bit. Header253-298 confirms V2 layout. Current native access and allowlist match.

| Operation | AP offset/width | Source and treatment |
|---|---|---|
|SLAVE_ADDR|04/16|slave<<1; only34/37; source1062-1066|
|INTR_MASK|08/16|source0x129; port0 pending explicit IRQ ownership resolution|
|INTR_STAT|0c/16 W1C|source1075,1697-1701; port clears only owned1/3, not unknown events|
|CONTROL|10/16|28 write,3a WRRD; noDMA/WRAPPER/IRQ_SEL; c991-1008|
|TRANSFER_LEN|14/16|3 write or1 pointer; c1084-1104|
|TRANSAC_LEN|18/16|1 write or2 WRRD|
|DELAY_LEN|1c/16|10; c1023-1025|
|TIMING|20/16|source-calculated plus timeout enable; c1049-1052|
|START|24/16 command|literal1 only, ordered; bit0busy; never write inherited2 back|
|EXT_CONF|28/16|8001 at100k; c1011-1012|
|LTIMING|2c/16|independent source helper same count at100k|
|HS|30/16|0, source speed helper disables high-speed transaction|
|IO_CONFIG|34/16|open-drain3; AED extension absent/common disabled|
|FIFO_ADDR_CLR|38/16 command|5 normal/multichannel; no HFIFO clear2|
|MCU_INTR|40/16|1, source1202-1203, now last configuration write|
|TRANSFER_LEN_AUX|44/16|2 WRRD; port normalizes0 forwrite and restores; source write path leaves staleAUX unused|
|CLOCK_DIV|48/16|0404, DT5; c978-985|
|HW_TIMEOUT|4c/16|ceil2ms low-period count; c963-977|
|DATA_PORT|00/8|pointer and MSB/LSB writes, twoBE byte reads; c1191-1198,1343-1353|
|DMA_FSM_DEBUG|ec/16 read|I3C_EN evidence preserved, optional HFIFO header only>1MHz; c1040-1045|
|FIFO_STAT|f4/16 read|conservative inherited emptiness guard, measured0|

Private setup sequence: timeout,divider,CONTROL,EXT,DELAY,IO,TIMING,LTIMING,HS,address,FIFO5,mask,length,AUX,transactions,byteFIFO,MCU_INTR1,START1. Polling mask remains the only unresolved normal-path hardware parity decision. Source IRQ status clearing and port's exact priorIRQ adoption intentionally differ to preserve ownership.

## Handoff, arbitration and hidden state

- AP START2 is measured and source-supported idle readback: TRANSAC_START bit0 vs RESUME_ARBIT bit1, h55-56; source wait_done404-427 tests bit0. Normal START1 is the sole launch. Shadow/CCU remain conservative mask3 checks. These are unowned channels; do not issue shadowRESUME or reset to satisfy a guard.
- CAM domain is off in both actualstatus words, CCU/shadowSTART andCONTROL/FIFO checks passed before capturedAPstate. APDMA clocks are both ungated; APDMAEN read passed. There is no I2C8-specific hardware semaphore in selected i2c_get/release_semaphore: only gpupm support is conditional (c368-399), absent on this node. Linux's AP mutex corresponds to the single XNU worker/software lease; hardware channel arbitration remains untouched.
- Linux probe resets shadow and DMA globally (c429-459,2009-2017). The port deliberately adopts an already initialized firmware bus and never repeats global reset, because DDR/UFS bucks share it. Actual AP CONTROL28/IRQ1/START2 provides inherited activity evidence but does not prove a future transaction will ACK.
- V2 MULTI_DMA0xf8c is secure-programmed via SMC only on Linux resume_noirq (c2155-2163); no corresponding first-probe write exists. PIO transactions do not enter the DMA setup path (lengths<=8, c922-930). No source basis exists to introduce a new secure-mode write or read of an unvalidated register merely to duplicate Linux resume.
- FIFO64/slaveADDR1, PMIC PATH_DIR, DCM, AED patch, HS master-code/HFIFO, CCU/hardware trigger, packedIDVFS length and DMA handshake/reset are all conditional features whose selecting predicates are false here. Their absence is not an omitted mandatory initialization.
- Return-time observations cannot prove no secure firmware will ever start the AP channel later. The operating contract is exclusive AP ownership by this XNU worker; conservative CCU/domain guards plus hardware arbitration remain intact. Do not present a software lease as a hardware semaphore.

## Completion, errors and restoration

Clean success requires exactIRQ1 and STARTbit0clear, then byte read, ownW1C1 and verifiedIRQ0, FIFO5, reverse restore15owned configwords. Source Linux keeps transfer config rather than restoring it; restoration is this port's ownership feature. START/FIFO/W1C are actions and cannot be restored like ordinary saved words. Inherited START2/DEBUG8800 stay read-only.

Clean multi-channel NACK requires exactIRQ3 and STARTbit0clear; source1326-1330 returnsremoteIO without reset/arbitration. Port clears ownIRQ3, verifies0, clearsFIFO5 and restores. No next amplifier after uncertain bus outcome. Unknown IRQ combinations/IBI/timeout/buserror/arb-lost remain sticky uncertain. Android may reset/recover or incompletely classify some errors; the port intentionally does not reproduce a shared-bus destructive recovery.

Timeouts differ: Android adapter2s (c1911); port setup20ms, stream2ms poll ceilings. Wire time at100k is under0.5ms for these requests, but clock stretching is not proved by tests. Port's shorter ceiling protects the21.33ms SRAM ring and stops uncertain; no blind retries. It could safely refuse a physically slower transfer, which would require actual evidence to tune.

Restore writes are barriered but not individually verified in baseline source. Standard successful MMIO writes are assumed committed, like Android. Newly added completion readback closes the concrete pendingIRQ return defect; parent GIC ownership must keep any enabled/pending interrupt bounded through restore and fail-safe handling. No evidence supports adding uncontrolled diagnostic reads after a callback failure.

## Actual evidence and remaining physical unknowns

Latest69words extracted from62a89dcc log. Compared with8e6697a2, only index50 GPIO DIN32 differs in unrelatedbit9; all owned clock/power/pin words are unchanged. Actual APfivefields are exactly START2/CONTROL28/IRQ1/FIFO0/DEBUG8800. The known software gates now have evidence; no future ACK/revision/POR/electrical/CLKS behavior is claimed measured.

Tests: verify.py passes source timing oracle, source-order read/write traces, sticky/dropped completion and failed-readback tests, baseline fault reproduction and normal NORD2ARM64 compile. Independent actual69snapshot lifecycle overlay passed threecycles/1335 simulated transfers plus fournegativecases: cold-state/verification/candidate-20261023T193900.115906Z/results.json. These tests simulate uncaptured controller/amp behavior and do not prove physical sound.

Cross-component findings relayed to parent: both amp outputs must be disabled before potentially50ms first-amp powerdown wait (install agent owns fix); AFE protection releaseACKclear is stricter than Android's explicit ignoredACK at pg3351-3353 (parent owns request-register verification decision). Neither is silently treated as resolved here.
