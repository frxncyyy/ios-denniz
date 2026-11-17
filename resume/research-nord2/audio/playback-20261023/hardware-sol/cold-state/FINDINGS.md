# Actual8e6697a2 inherited-state audit

Implementation intentionally deferred to Claude lead. No canonical or device edits.

## Reproduced evidence

`actual-snapshot.json` and `.inc` preserve all69 log words; `test-actual.c` links actual canonical audio_afe.c/audio_id.c. ASan/UBSan with strict warnings passes and reproduces gate0xf8 exactly. AUDIO is off with exact0x1112; ADSP is ON, bothstatus22 set and ADSP_PWR_CON masked0x224d. This is not a speculative transient. Pins32/33/35 alreadyexactI2S3 and current correctedpin gate passes.

f8 = ADSP_ON08 + PROTECT10 + INFRA20 + MUX40 + APLL2_ON80. No AUDIO_ON or pin error. The current gate encodes an overly narrow wholly cold clock-tree assumption.

## Required ownership design

1. Preserve ADSP domain unchanged. Selected `drivers/clk/mediatek/clk-mt6885-pg.c:507,509` gives separate AUDIOprotectbit4 and ADSPprotectbit3. AUDIO sequencing3267-3355 never changes ADSP registers or status22. ADSP power sequence3370-3460 matches inherited0x224d. `clk-mt6885.c:1203-1210` ADSP mux parents contain main/univ/adsp PLLs, no APLL2. Accept stable coherent ADSP-on alongside AUDIO-off only with live-state revalidation; do not power it down to satisfy gate.
2. APLL2 already ON/isolated-off at correct196.608MHz. Inheritedpostdiv4/PCW78fd5264 differ from current coldprogrammer's postdiv3/PCW3c7ea932 but `naf_pll_rate` gives same196608000. Tuner78fd5265 is alreadyPCW+1, globaltuner bit5clear. Do not reprogram/shutdown this borrowed PLL. Preserve full inherited rate/power/tuner fields, including bit31/otherstatus bits; retain existing coldprogram path for coldPLL. Source `clk-pll.c:245-279,426-459,524-551` defines PCW/tuner and prepare/unprepare; generic CCF ownership must be represented explicitly here rather than assuming enabledmeansowned.
3. Infra audio and audio26m are alreadyungated. Source gate aliases `clk-mt6885.c:2115-2136` support enabling; stop must restore snapshot fields or retain borrowed sharedclocks, not unconditionallySET gatingbits. Current unconditionalstop would disable inherited clocks and fail `naf_live` verification even if gatewerejustremoved.
4. Existing audio parents: CFG7audio1=mainpll_d5_d8, CFG8intbus1=mainpll_d4_d4, CFG12engen2=apll2_d4, CFG13aud2=apll2_ck, CFG15audio_h=apll2_ck. Sources `clk-mt6885.c:1031-1042,1166-1170,1198-1200,1290-1294,1834-1850,1922-1948,1985-1989`. AndroidAFE enable explicitlyreparentsaudio/intbus26MHz (`mt6885-afe-clk.c:246-260`) andaudio_hAPLL2(:264-272). Either narrowlyadoptproven inheritedparents or source-exact normalizeaudio/intbuswithsnapshotrestore; do nottouch ADSP's neighboringCFG13field, UFS neighboringCFG12fields, or main/univPLL. aud2/engen2 alreadydesired playbackparents; preserve their inheritedungatedstate onstop.
5. AUDIOprotectbit4 is clear while domainoff. Androidon path clearsprotectionafterpowerup and doesnot requirepreexistingprotection (`pg.c:3314-3355`). Coldprotect1 gate is notnecessary for theon sequence; if normalizationSETSbeforepowerup, it must bebit4only, pollack, and restoreinheritedbit4 onstop aftersafeoff. Preserve ADSPprotectbit3 and allotherbits. Currentstop leavesbit4set, causingreadbackmismatchfor actualsnapshot.

## Downstream evidence and remaining unknowns

Known snapshot preconditions pass current narrow I2C8 code: i2c_selparent2/univpllvalidpowered; I2C8gatebit4clear; GPIO122/123mode1, IESmask2008set, bothDIN26/27high; CAMstatus23clear (CCUmasterdomainoff). Reset111/148 are modeGPIO/outputlow (released); source5msassert/deassertpulse remains appropriate. Noresetguardblocker is demonstrated.

APDMA clocks BOTHungated, so preflight WILL read DMAenable at11000788. That register, threechannelSTART/CONTROL/FIFO/IRQ/I3Cstate, actualamprevisions, PWDN/AMPE/MANSTATE, and postpowerAFEdefaultstate were NOT captured; cannot claim they pass or relaxthoseguards. Preserve each stagediagnostic. No boardprofile compatibility proof before actualrevisionreads; accepted board remains exact9874-only.

SourceAFE enable also selects audio_h=APLL2; currentprogram doesnot configurethatmux, but ACTUALsnapshot alreadyhas parent3andungated, so it is not an imminent blocker for this image. Require livewatch of borrowedCLK_CFG15 audio_h bits to detectdrift; do not gratuitouslyownotherCFG15fields.

ExternalMCLK is conditionalonrequestedmclk_rate (`mt6885-dai-i2s.c:1101-1125`); no proof that missingMCLK operation blocks this no-MCLKthreewireamp route. Do not add a guesseddivider based solelyonAUDDIVsnapshot.

## Required regression scope for proposed patch

Useactual69words, stableADSP-on plus AUDIO-off; fullinit/start/feed/stop≥3cycles; allborrowedPLL/ADSP/clock/mux/protectfieldsrestored bitexact (exceptdocumentedkeyedSPMhighhalf). NoADSPwrites or APLLpower/PCWwrites forborrowedPLL. Reject mismatchedpoweredPLLrate, incoherentdomainstatus, unsupportedmuxparents, andlivechangesbeforewrites. Preserve originalcoldpath tests. Inject powerup/protecttimeout andchecksafeuncertainhandlingwithoutclaimingrestoration. Verify actualI2Cpreflight model separately ratherthanmockingallclocks/PINchecksaway.

## Parent coordination note

Also review `../../install-sol/startup-audit/FINDINGS.md` and its private deadline patch. A separate source-based audit reproduced the one-second tone deadline expiring during slow startup, plus synchronous startup logging while DMA is active. The proposed device-wrapper-only fix and source-extracting sanitizer regression are ready; no canonical edits have been made.

Additional authorized parallel startup timing/logging audit: [install-sol findings](../../install-sol/startup-audit/FINDINGS.md). It demonstrates diagnostic duration including setup latency and synchronous logging while DMA runs; private fixes and regressions are available there.
