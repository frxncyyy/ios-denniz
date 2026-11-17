# Independent post-preflight/controller/amplifier audit

Actual62a89dcc log: init refused before writes at stage8/failure21;
START2 CONTROL28 IRQ1 FIFO0 I3C8800, savedvalid1f. That is not evidence of an
active transfer, FIFO residue or a completed amplifier probe. Reviewed selected
Android source in sibling nord2-current-kernel (documented pin04d00c2), canonical
transport/amp/board code and pinned635300fb... container. No phone/canonical/CI
access or edits. audio_hardware owns the I3C inherited-capability candidate.

## Source-backed conclusion

No additional demonstrated post-preflight startup blocker found in this bounded
audit. The I3C guard correction must preserve0xec/8800 untouched; do not create a
mode-clear write. Android i2c-mtk.c1040..1045 only uses I3C_EN to select optional
HFIFO header at speed>1MHz and !hs_only. Our100kHz PIO path never enters it.
Unknownbit11 is not decoded or modified. This agrees independently with
hardware agent's candidate; it is broader than just assuming bit15 is idle.

| Area | Selected source and canonical result |
|---|---|
| Device configuration | mt6893.dts6712..6723 +6908..6930: V2, cnt_constraint1, set_ltiming1, set_dt_div1, div5; control_irq_sel/set_aed absent0, FIFOwidth defaults8. |
| Legacy100k programming | i2c-mtk.c983..1085: CONTROL28(write)/3a(WRRD), EXT8001, DELAY10, IO3(open-drain), HS0, auxread2/transac2 or write3/transac1 match. No DMA path for<=8bytes. |
| Timing | calculate_speed468..532 countconstraint special-case is reproduced; TIMING timeoutbit0, LTIMING sample/step and HW_TIMEOUT963..977 match. No off-by-one fix warranted. |
| Save/restore offsets | All15 are V2 writable configuration fields. 0x30 is HS and0x34 IO_CONFIG, not legacy FIFO fields; FIFO_STAT is0xf4. Reverse restore uses saved values; START/0xec/shadow/CCU/DMA are never restored/written. |
| IRQ1 adoption | Rechecks idle/shadow/CCU/AP/FIFO, W1C exactly1 and readbacks0 before setup. Incoming status outside0/1 remains refused. IRQ masking0 is compatible with status latching; Android IRQ handler1697 masks before reading status. |
| Completion | ExactCOMP1 and completedmulti-channelACKERR3 handling preserves bounded no-reset source path1326..1330. Timeout/BUSERR/otherstatuses remain uncertain/offline; no repair/retry/nextslave after uncertainty. |
| PIO bytes | 8bit registerpointer +16bitBE data matches tfa98xx regmap8/16 and selected TFA routines; writes remain exactly3bytes, reads repeated-start1+2. |
| Silicon init | Independent executable comparison: all5 nonempty canonical errata tables exactly match tfa_init.c cases0a73/0b73/0a74/0b74/0c74;1a73 intentionally empty. Key protocol matches tfa_dsp.c819..830. |
| Board script | All104selected fields independently decoded directly from pinnedblob match stored decode and currentoffsets;52peramp. Fields preserve PWDN/AMPE and AUDFS; hardware sets AUDFS8 after profile, checks powereddown/outputdisabled before enabling. |
| Operating order | tfa_dsp.cOPERATING: PWDN0 then MANSCONF1 then AMPE1 for probus. Canonical fields match9874 PWDN0000/AMPE0030/MANSCONF0120/AUDFS0203/CLKS1170/TDMERR1060/NOCLK1070. |

`audit-source-profile.py` PASSes exact errata/profile comparisons and stores
source-profile-receipt.json with every source hash. This is source/format
validation, not proof of register effects. Hardware agent is separately exercising
all65536inherited0xec values and repeated full lifecycles with8800.

## Concrete remaining runtime gates (not reasons to relax checks)

The next unseen operation is exact inheritedIRQ1 adoption/write/readback, followed
by15-word AP snapshot/program, FIFO transaction and physicalACK/REV read. Actual
amp revisions, addresses after reset, board-compatible9874 revision, clockready
and TDM/NOCLK results remain unobserved. Supported profile is0a74/0b74/0c74 only;
9873 audible start correctly refuses rather than applying another chip's profile.

Successful callback writes are treated as completed MMIO by this transport, as
in the Android driver. Normal15-word configuration restoration has no explicit
readback loop; existing models compare the restored register image. That is a
verification limitation, not a newly observed hardware failure or justification
for adding speculative extra reads/writes before this next test. Keep source
failure diagnostics and stop/reset containment intact. Do not claim the I3C
correction guarantees audible playback; it removes a proven false prerequisite
while later physical gates continue to fail closed.
