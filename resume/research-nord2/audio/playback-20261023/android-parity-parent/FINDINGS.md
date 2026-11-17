# Parent integration audit: selected Android04d00c2 vs current XNU audio

## Peripheral IRQ versus GIC delivery
Selected mt6893.dts6913 assigns I2C8 SPI120, therefore INTID152, GICD_ISENABLER4 bit24. Selected i2c-mtk.c1068–1081 derives AP mask0x129 (COMP, arbitration, hardware timeout, bus error; excludes NACK bits when ch_offset is nonzero). Prior claim that the IRQhandler masking then reading status proves completion latches while never enabled was not established: the source handler runs after an enabled interrupt already latched. Coreboot and U-Boot polled MTK drivers also enable peripheral completion/error bits; their different chips are supporting evidence, not the Nord2 register contract.

Implemented exact selected APmask0x129, with read-only checks of GIC delivery disabled before any bus mutation and immediately beforeSTART. No distributor mutation/takeover. One exact-address native read reuses PE gicd_base via nord2_audio_gic_read; no tenth mapping or audio dependency on WiFi globals. Foreign enabled owner refuses before mutation; a late owner/read failure aftersetup becomes stickyuncertain and neverSTARTs. Model tests exercise both cases and unrelated interrupts. Completion W1C is verified before restoring inherited APmask.

## AFE/clock/format source review
Selected mt6885-mt6359.c415–424 separates16bit FE PCM from32bit BE I2S slots; current DL1 S16 packed stereo + I2S3 WLEN32 is consistent. Optional mtk_spk_i2s_mck only connects when configured; no selected board requirement for an additional invented MCLK output. Dedicated aud_i2s4_bclk gate exists in clock declarations but no selected sound-driver reference warrants toggling it. Preserve unrelated ADSP/audio_h and borrowed196.608MHz APLL2; already tested inherited tree retains exact parents/gates. Physical CLKS and moving DMA remain unmeasured.

INFRA_TOPAXI_PROTECTEN_2 request1710 differs from ACK1724: selected clk-mt6885-pg.c3350–3353 explicitly ignores ACK afterCLR. New request readback should replace stricter releaseACKclear precondition; powerdown must verify requestSET plusACK1 (stale ACK1 cannot prove a droppedSET succeeded). Hardware agent owns source/test patch. Snapshot newword must be explicit synthetic in old69word fixtures.

## Integrated amp and lifecycle audit
Installer candidate resets I2C defaults before silicon settings and replays exact devicecontainer keypreamble; all volatile operations are allowlisted and no MTP command is introduced. Normalstop mutes bothamps before anyMANSTATEwait; stop bound aligns toAndroid100ms. Fatal DMAownershiploss immediately holds ownedlocal reset before potentiallyblocking I2C and closesAFE, preservinguncertainty. Runtime high-gain/protection DSP, microphone and telephony are out of playback scope.

Producer candidate accepts full legal16384frameHALbursts and accounts for queuednonzeroaudio across delayedstartup. Private immutable receipts preserve demonstrated oldfailures. Comprehensive final host/native/tests and independentreview required; no physical success inferred from simulation.
