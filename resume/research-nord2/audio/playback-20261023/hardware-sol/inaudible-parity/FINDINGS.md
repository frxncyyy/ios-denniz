# Confirmed inaudible playback route defect

Actual d0677caa hardware now initializes both TFA9874 revision0c74, completes silent DMA and PCM/tone streams with no transport/clock fault. This proves control and DMA progress, not that DL1 reaches the selected serial output.

## Defect and narrow correction

Selected Android04d00c2 `sound/soc/mediatek/mt6885/mt6885-dai-i2s.c:407-433` defines I2S3 channel mixers with AFE_CONN0/1 and I_DL1_CH1/2 bits5/6. DAPM widgets813-818 bind these exact mixers to I2S3; routes1302-1305 and1340-1343 connect DL1 through them. `mt6885-reg.h:2361-2362` gives offsets20/24, physical11210020/11210024. Interconnection.h17-18 supplies input bits5/6.

The implemented CONN28/29 (112104bc/c0) instead belong to I2S1 mixers (`dai-i2s.c:461-483`, widgets807-812). I2S3 can therefore clock cleanly while its normal mixer receives zero. This directly explains clean DL1_CUR progress and no sound. Earlier SOURCE-AUDIT citation462-501 was the wrong I2S1 mixer block; this audit corrects that error explicitly.

Patch changes open/close route RMW targets to CONN0/1 and replaces their write allowlist entries. Historical CONN28/29 macro addresses and NAU_A observation slots7/8 remain unchanged/read-only. New CONN0/1 observation slots append at9/10; none are relabeled as historical measurements. The entire70-word boot snapshot fixture is byte-identical; no added synthetic boot word. Existing synthetic AFE sentinels in tests are expressly identified as such.

## Remaining data-to-pins crosscheck

- SRAM packing is correct for stereoS16: lower16bits left, upper16bits right, little-endian32bit store. FE HD0 is selected by Android common/mtk-afe-fe-dai.c929-932, stereo usesMONO0, normalSRAM bit5 matches mt6885-afe-pcm.c3500-3508. SRAM11212000/4KiB remains within the same AFE16KiB mapping, no alias. Native writes remain ordered and unchanged.
- CPU_HD_ALIGN0 and both CONN_24BIT registersffffffff exactly match Android runtime resume `afe-pcm.c3465-3471`, which explicitly sets ALL output ports to24bit. Thus corrected outputO0/O1 is already covered, as were wrongO28/O29. No width-mask change is needed. Tests additionally assert lowbits0/1 set.
- FE S16 and BE32 are intentionally independent. Machine fixup `mt6885-mt6359.c415-425` selects S32_LE on I2S; `get_i2s_wlen`55-58 maps that to WLENbit1=1. I2S3 config1803-1808 selects I2S formatbit3, rate48k enum10 and AFE_I2S_CON3. Existing plan matches.
- I2S_CON3 bit13 (I2S4_32BIT_EN) is the alternate tinyconn path, toggled by tinyconn event725-743. It is NOT the ordinary serial32bit width. Normal matrix route must keep bit13clear while WLENbit1set; the existing config mask clears13 correctly. New regression asserts both independently.
- GPIO32/33/35 function1 is I2S3 BCK/LRCK/DO in pinctrl-mtk-mt6885.h377-418, matching exact DT1737-1757. Function2 would select I2S1 and is not used. Pin31 externalMCLK remains untouched; source MCLK dependency is conditional and actual amp CLKS now succeeds. No basis for adding a guessed MCLK divider or changing pins.
- Existing container slot/sample widths and device-specific left/right selection remain source/profile-defined. A slot swap could exchange channels, but cannot explain both silent copies of the mono test tone while matrix is disconnected. No unproven TDM alignment or analog gain table is introduced.
- Software gain remains conservative256/32768 (-42dB full-scale ceiling); tone wave amplitude16384 yields -48dBFS peak after scaling. This limits listening volume and should be considered if later playback is merely faint, but does not justify changing gain together with the proven route repair.

No second source-proven data-path defect found. Physical serial data/significance and acoustic output remain unmeasured; model tests do not claim otherwise.

## Independent regressions and receipts

`route-source-oracle.h` is generated from the selected Android mixer declarations/register/input definitions, not from port macros. `route-source.json` records extracted values and source hashes.

- Baseline canonical plan reproduced: DL1 connects I2S1, I2S3 remains disconnected.
- Patched source oracle verifies CONN0/1 bits5/6 connect AND disconnect, zero CONN28/29 writes.
- Full actual-code model initializes known0c74/0c74 and performs3 start/feed/stop cycles with distinct nonzero left/right samples; verifies packedS16 words, independent32bitserial format, normal matrix selection and output width. Unrelated synthetic I2S1 sentinels remain unchanged through open, stop, running-pointer failure and failed powerup.
- Existing inherited plan/cycle/mixed/power/protection/drift regressions pass. Historical observation enum indices/PAs and complete boot fixture remain untouched.
- Normal NORD2ARM64 compile of affected AFE/id passes.

`verify.py`, `verification/results.json`, `verification/source-sha256.json` retain the runs. Durable test-route-plan.c (always) and test-route.c (private board model) are registered in the proposed tools patch. `combined.patch` is the final reviewed bundle; route-runtime.patch is runtime-only and supersedes the earlier draft. No canonical/device/CI changes by this agent.
