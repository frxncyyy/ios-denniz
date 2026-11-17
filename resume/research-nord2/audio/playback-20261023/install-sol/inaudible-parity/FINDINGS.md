# Inaudible rev0c74 playback: amplifier parity and bounded saved diagnostics

The AFE route error identified by the hardware agent is a concrete explanation for clocks and DMA progress without audio reaching the speakers. I found no additional demonstrated missing TFA9874c unmute operation. The proposed patch adds observations, rather than guessing new amplifier configuration or increasing gain.

## What the physical evidence establishes

The source receipt identifies image d0677caa, source23ebee50313dea81c3c1da8c6a1ee84484cec87a and CI40015584829. Both physical amplifiers identify as0c74, supported by the exact selected Android silicon table and pinned9874 container. Silent DMA and the diagnostic tone complete without errors. Video sends accepted PCM with no producer drops. A clean stop succeeds.

In `ios-parity/hardware-review/status-hal-1.json`, status0/status1 are **decimal22 = hexadecimal0016**. Those bits are boost-OCP-ok, thermal-ok and undervoltage-ok. MANALARM0x20, TDMERR0x40 and NOCLK0x80 are clear. This is not hexadecimal0022 and not an alarm observation. The existing status is register10 only; it does not prove the physical amplifier is engaged or in MANSTATE9. Fields11/13/14 and the untouched mute/TDM enable bits are not in the current saved evidence.

## Selected Android comparison after board profile

Paths below are under sibling `nord2-current-kernel/sound/soc/codecs/audio/tfa98xx`.

- `tfa_dsp.c298–306` selects family2, proBus, one speaker and TDM for9874. `tfa_init.c1029–1044` installs `tfa_set_mute_nodsp`; its body at174–181 ignores the mute argument and returns success without a hardware access.
- `tfa98xx.c3994–4014` performs synchronized `TFA_STATE_UNMUTE` after both devices start. `tfa_dsp.c4398–4399` calls the mute-off wrapper, whose family2 branch at1060–1074 invokes that no-op and writes AMPE1. The port already writes and reads back AMPE1. Calling an additional generic unmute would repeat that write; it would not reveal a missing digital-mute register.
- `tfa_dsp.c4330–4345` uses PWDN0 → MANSCONF1 → AMPE1 for proBus. SBSL belongs to the non-proBus branch and must not be copied to this chip. The port implements that order and waits for CLKS before AMPE. SYS0 readback establishes the requested control bit, but not asynchronous MANSTATE/SWS/AMPS completion.
- `tfa_init.c945–963` has the exact0c74 table:13 literal writes, beginning register02=22c8. The port has the same values and order, after I2CR reset and documented key handling. Unlike older0a74/0b74 tables, this selected0c74 table does not write register58. Copying an older revision's58literal to clear an imagined mute would be unsupported.
- The selected speaker profile preserves the source32-bit slots: amp34 speaker slot1, amp37 slot0. It has five register fields and a default marker, no vstep, filter or DSP data payload. The device's six-byte `74ffff000000` dummy patch contains no patch records. No omitted volume-step file was found.
- Field `hard_mute` is register58bit1 (`tfa9874_tfafieldnames.h514`). Profile register58mask01f0 changes PWM delay, not hard mute. TDME is20bit4 and TDMSPKE is23bit0. The pinned profile and exact0c74 Android table leave those three bits at reset defaults. They were not observed on this physical run. This is a reason to read and save them, not to invent new values.
- Android's runtime analog-volume mixer at `tfa98xx.c454–475` writes TDMSPKG only when userspace changes that mixer. It is not called by the selected startup path. The container already supplies TDMSPKG4/5 and AMPGAIN190. Source names do not give a trustworthy conversion of those raw values to dB. Do not change them speculatively.
- Android sends impedance/calibration data to its host DSP at `tfa98xx.c4320–4393`; startup invokes that during playback. Its proBus operating/unmute branches still explicitly set AMPE. There is no source-backed on-chip DSP image upload or calibration command needed to make this register-only speaker profile unmute. The missing host SmartPA algorithm remains a processing/protection limitation; this port intentionally does not program calibration/MTP.

The independent DMA ceiling is1/128 (−42.144dB). The half-scale tone peaks at128 after that ceiling (−48.165dBFS). This can make a valid test quiet, but it does not justify raising the ceiling before correcting the proven route error. HAL volume0 in the receipt means no software attenuation was requested; it is not a hardware mute value.

## Private diagnostic patch

`startup-observed.patch` modifies only `audio_hw.h`, `audio_hw.c`, `audio_i2c8.c` and `audio_device.c`. `manifest.json` records baseline and candidate hashes. No canonical, device or CI edits were made.

After both AMPE enables/readbacks, and before any queued PCM is fed, it captures a fixed nine-register list for each amplifier:00,10,11,13,14,58,20,23,26. These18 transactions add approximately several milliseconds at the existing100kHz rate while the DMA ring still contains zeros. There are no ongoing tick reads or new amplifier writes. Only STATUS13 is newly permitted as a read; its writes remain denied before MMIO. No generic raw-register API is added.

Each startup clears its saved values and valid masks and advances a sequence. A transfer failure stops the observation immediately and follows existing startup cleanup; partial masks retain exactly which values were read. STATUS10 can clear NOCLK on read, so a captured NOCLK/TDMERR is saved and immediately reported as the existing AMP_FAULT startup failure instead of being silently consumed by diagnostics.

The worker prints two `NORD2 AUDIO AMP START` records at most once per startup, only after normal stop or failed startup. Printing uses saved memory only; the running DMA feeder never prints these records. They preserve evidence from before shutdown changes AMPE, reset and state.

Interpretation for the next run:

| Saved field | Relevant meaning |
|---|---|
| sys | PWDNbit0 should be0, AMPEbit3 requested1; DCAbit4 from speaker profile |
| s10 |0016 is healthy observed supply flags;20 alarm,40 TDM error,80 lost clock |
| s11 | CLKSbit7; SWS/engagebit13; AMPS/amp-enabledbit14; AREFSbit15 |
| s13 | MANMUTEbit14; MANOPERbit15; other protection flags retained raw |
| s14 | MANSTATEbits6:3; selected source names9 operating,10 mute,8 alarm |
| mute58 | hard_mutebit1; do not infer it from profile PWM-delay fields |
| tdm20 | TDMEbit4; raw TDM clock/frame fields also retained |
| tdm23 | TDMSPKEbit0, TDMDCEbit1, current/voltage feedback enables |
| tdm26 | speaker slotbits3:0 should be1 for34 and0 for37; DC/current slots retained |

These are immediate startup observations, not a new requirement that every asynchronous flag already be settled at that instant. No unsupported MANSTATE/AMPS predicate is introduced.

## Verification

`test_observed.py` extracts the actual candidate capture/report functions at test time and compiles them with strict warnings, ASan and UBSan. It verifies exact18-read order, every failure position, partial masks, NOCLK preservation and fault handling, no tick capture, and reporting once only after running clears. The transport test verifies STATUS13 reads on both owned amplifiers and denial of writes before MMIO. All pass.

The existing full hardware model also passes against the four-file candidate:2179 amplifier transfers, normal startup, continuous PCM, stop, restart, persistent clock/TDM faults and ownership restoration. Three durable test files are under `candidate/tests/nord2-audio`; `test-suite-hook.patch` registers the runner. Physical operating flags and audibility remain unverified until the corrected route is tested.

## Final combined canonical review

Parent promoted the route correction and diagnostic patch. Independent read-only review confirms I2S3 CONN0/1 with DL1 bits5/6 matches selected Android407/432. Old CONN28/29 observations retain their indices and are no longer writable. The collector remains startup-only, after both amplifier enables; the worker reports saved values after running clears and deduplicates by sequence. STATUS10 NOCLK is not consumed silently. No additional ongoing reads, amplifier writes, raw interfaces, or playback-time diagnostic printing were introduced. The observed runner and two route tests are each registered once. No further source-level defect found. Source-review approval given; parent is running required suite/native checks. Exact hashes are in canonical-review.json.
