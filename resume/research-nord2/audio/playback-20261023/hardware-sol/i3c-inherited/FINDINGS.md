# Inherited DMA_FSM_DEBUG8800 on I2C8

## Conclusion and exact change

No debug-register normalization is required or source-backed. Retain0x8800 unchanged as diagnostic evidence, use the existing fixed100kHz legacy-I2C transfer setup, and restore only its15 already-owned AP configuration words. Do not write0xec, clearbit15/bit11, programHFIFO, reset the controller or touch another master. Private audio_i2c8.c.patch removes only the I3C_EN refusal and its unusedmacro, with source rationale; headers-comment-only.patch retires stale claims. No raw-write footprint changes.

Bit15 need not be classified as purely capability or purely active mode: the selected driver's explicit control flow permits legacy transfers while it is set. Its name alone does not authorize rejection. Meaning of bit11 in8800 is not documented in these sources and is not invented.

## Source contract, selected Android04d00c2

Sibling nord2-current-kernel/drivers/i2c/busses/i2c-mtk.h33 defines I3C_EN=bit15,295 assigns V2DMA_FSM_DEBUG0xec;100 defines MAX_FS_PLUS_MODE_SPEED=1000000Hz. The sole functional read/use in i2c-mtk.c1040-1045 samples bit15 and writes HFIFO header ONLY if bit15 && configuredspeed>1MHz && !hs_only. No write to DMA_FSM_DEBUG exists. At100kHz that branch is false for EVERY16-bit debugreadback.

The complete legacy setup is source c554-617 timing/HS0; c991-1008 CONTROL28 forwrite or3a forWRRD; c1011-1012 EXT_CONF8001; c1025 DELAY10; c1028-1037 open-drain3; c1049-1055 TIMINGtimeout/LTIMING/HS0; c1075-1079 FIFO5; c1202-1213 MCU_INTR1/barrier/START1. Existing port already programs these values, bounded to<=100kHz by derived parent/timing arithmetic. I3C master-code header and HFIFO clearbit2 are not executed by source at100kHz, so introducing either here would add unsupported traffic.

APDEBUG0xec is outside the15 saved writableconfigwords; preserving it requires no restorewrite. Original config is restored in reverse on exactcompletion orcleanNACK. START2 remains readbackonly, W1CIRQ1 adoption is explicit, DMA/CCU/FIFO/pad/clock/busy checks remain intact. Existing native writeallowlist denies debug/HFIFO/reset/foreignchannel writes.

## Actual later preflight evidence

Saved APvalid1f now proves START2, CONTROL28(noDMA), IRQ1(exactcompleted), FIFO0, DEBUG8800. Thus every source-based preflight predicate needed before our first ordinary transaction has been observed valid; there is no additional known inheritedAPsoftwaregate after removing the falseI3Crefusal. The next operations are duplicateidle checks, exactW1C1 of inheritedcompletion, rereadIRQ0, then saved15words/FIFO/config/data/START1. A stickyIRQ or newbusy transition must still stop safely.

Actual revisionreads, cleanNACK versus timeout/busfault, externalresetresponse, exact9874boardmatch and POR PWDN/AMPE/MANSTATE remain physicallyunmeasured. Do not invent workarounds for those. Testfixtures explicitly simulate them.

## Tests

Private test-i3c.c passes ASan/UBSan with strictwarnings: all65536 inherited16-bit values acrossread16+exactIRQ1adoption/APSTART2, preservingdebugword and15configwords; measured8800 additionally exerciseswrite16, cleanNACK and nextslave; seven busy/DMA/FIFO/IRQ/CCU guards still refusebeforewrites. Strictmodelallows APconfig/normalFIFO/START1 only and therefore aborts on debug/HFIFO/reset/shadow writes.

Independent fullactual69snapshot harness now seeds measuredAP START2/CONTROL28/IRQ1/FIFO0/DEBUG8800. Against privateoverlay it passes threeinit/start/feed/stopcycles (1335 simulated transfers), all69sharedfields restored, debugword andAPSTART2 retained, no borrowedPLL/ADSPwrites, plus allfour negativecases. Receipt ../cold-state/verification/candidate-20261023T192908.444224Z/results.json. NormalNORD2ARM64 compile of privateaudio_i2c8.c also passed. No canonical/device/CI modifications.
