# Register/source audit

All MediaTek/NXP register derivation uses selected Android kernel04d00c2565764c3922c9b4826b5acff1106e533f.
Pinned topaz/HackiOS references informed the parent HAL/character-device contract;
no Qualcomm clock, power, DMA or amplifier addresses were transplanted here.

| Hardware operation | Selected Android source |
|---|---|
| AUDIO parent clocks, power21, SRAM PDN/ACK, protectionbit4 | drivers/clk/mediatek/clk-mt6885-pg.c:3267–3355,5001–5003 |
| AFE clock enable/runtime resume | sound/soc/mediatek/mt6885/mt6885-afe-clk.c:212–298; mt6885-afe-pcm.c:3442–3478 |
| APLL2 PCW32 / integer8 / fin26MHz, Fmin1.5GHz, postdiv | drivers/clk/mediatek/clk-mt6885.c:2407–2442,2529–2534; clk-pll.c:245–325 |
| APLL PCW-change CON1bit31, tunerPCW+1, delay20us | drivers/clk/mediatek/clk-pll.c:245–279,608–613 |
| APLL power/enable and reverse disable | drivers/clk/mediatek/clk-pll.c:426–459,524–551 |
| APLL2 audio muxes/tuner/24M engine | sound/soc/mediatek/mt6885/mt6885-afe-clk.c:149–205,421–477 |
| I2S3 AFE_I2S_CON3 format/rate/word length | sound/soc/mediatek/mt6885/mt6885-dai-i2s.c:1754–1808 |
| I2S3 backend explicitly32-bit independent of FE S16 | sound/soc/mediatek/mt6885/mt6885-mt6359.c:415–425,873–880 |
| DL1 address/channel/format/rate/pbuf and SRAM mode | sound/soc/mediatek/common/mtk-afe-fe-dai.c:776–973; mt6885-afe-pcm.c:300–308,2004–2032,3500–3598 |
| DL1 channels5/6 -> I2S3 outputs28/29 | sound/soc/mediatek/mt6885/mt6885-dai-i2s.c:462–501 |
| SRAM physical11212000, block4KiB | arch/arm64/boot/dts/mediatek/mt6893.dts audio node (line numbers differ from prior expanded source) |
| I2C8 clock e04/e08 bit4 | drivers/clk/mediatek/clk-mt6885-i2c.c:110–135 |
| Shared I2C mux CLK_CFG_10 PDN15, parent preserved | drivers/clk/mediatek/clk-mt6885.c:1886–1890 |
| PIO standard write CONTROL28, WRRD CONTROL3a | drivers/i2c/busses/i2c-mtk.c:983–1055,1071–1203 |
| V2 I3C gate uses0xec (not0xe4) | drivers/i2c/busses/i2c-mtk.c:1039–1046; i2c-mtk.h:275,295 |
| Clean multi-channel NACK no controller reset | drivers/i2c/busses/i2c-mtk.c:1326–1330 |
| Active-high reset5ms/5ms; output-low pin request | sound/soc/codecs/audio/tfa98xx/tfa98xx.c:4740–4749,5037 |
| Silicon revision initialization six cases | sound/soc/codecs/audio/tfa98xx/tfa_init.c:791–969 |
| Volatile unlock key protocol | sound/soc/codecs/audio/tfa98xx/tfa_dsp.c:819–830 |
| Probus PWDN0 -> MANSCONF1 -> AMPE1 | sound/soc/codecs/audio/tfa98xx/tfa_dsp.c:4284–4347 |
| Exact family CLKS/status/control fields | sound/soc/codecs/audio/tfa98xx/tfa9873_tfafieldnames.h, tfa9874_tfafieldnames.h |

NAF low-level addresses and allowlist are in audio_afe.h/c. Native windows are
listed exactly in audio_hw_native.inc. RMW writes preserve non-owned bits;
SET/CLR/UPDATE aliases are explicit writes. The transport accepts slaves34/37
only. Each runtime/profile RMW must match the immediately preceding same-register
read and an allowed bit change. Silicon errata writes are exact source values.
Profile bit changes come only from the independently SHA-validated runtime plan.

Volatile key latch0xa1 is named MTPKEY2/MTPK by NXP; writing its unlock/lock value
does not issue a nonvolatile programming operation. No write to MTP row/copy/start
registers0xa3/0xa7/0xa8 or OTP data is admitted. No shared rail slave51/56 address
is admitted. No controller reset, shadow/CCU write, APDMA write or RESUME_ARBIT
is admitted. Board container provenance, offsets, decoded operation inventory,
firmware variant rejection and exact digest are maintained by profile-sol.

Streaming monitor I2C polling is capped at200x10us (2ms), versus the generic
2000x10us setup ceiling. It stops on the first transport fault, so a timeout
cannot consume a full21.33ms ring revolution. Amp clock-ready polls occur
before first tick over a zero-filled ring; stop's state-machine polls happen
after AMPE is cleared. No unbounded hardware polling loop is present.
