# Muffled-quality follow-up (source only)

Current image b60647f1. Parent reports healthy silentDMA+3quiet restarts,11media starts/no faults/drops, audible bottom improvement at2048 and subjective slightlymuffled sound; top256/bottom4096 listening pending. User has not isolated the physical speaker for this description. No device/source/build operations in this follow-up.

## Proven versus unresolved

HAL source audio_plugin.c304–315 advertises signed packed stereo16bit at REDWOOD_AUDIO_RATE48000; actual hardware path accepts only48000/2/16. audio_afe.c343–368 sets S16stereoDL1memory,48krate, zero CPU_HD_ALIGN and32bitI2S3serialslots. This is consistent with selected Android common/mtk-afe-fe-dai.c929–937 S16formatHD0 and mt6885-mt6359.c415–424 fixing BEserialS32. Widths describe different interfaces, not an established16/32mismatch. audio_afe.c436–439 now routesDL1CH1/2 through correctI2S3CONN0/1. Exact stockcontainer uses32bitslots/samplefields, inputbottomslot1/topslot0; runtimeAUDFS8 supplies48k. Prior saved startupregisters+audible isolation prove expected slots/clocks/mutes on both0c74. Actual packedDMA tests include sign/extrema/unity/ramp/channelisolation; source path adds scalar gain only, no deliberate low-pass/EQ.

No demonstrated current source-level rate/format/route defect explains muffling. These are source/model/readback checks, not an electrical serialcapture or acousticfrequency-response measurement. The1kHz tone is a singlefrequency: clear audibility would not establish fullbandwidth, pitchaccuracy, EQ or absence of distortion. No underrun/drop/fault does not prove fullacoustic fidelity. Muffled must not be rewritten as clipping/distortion.

BoardHPFBYP(reg51bit5)=0 and bypass_lp(reg53bit5)=1 are exactpinnedstock values; LPM1MODE2 and allanalog/filtersettings likewise sourcecopied. Do not bypass filters or swap profiles on the basis of this adjective. Their complete analog response/transitionlaws are undocumented here. Boardcontainer is A16sourced, not the provided unavailable Android13dump; crossrelease tuning equivalence is unverified.

Android DSP difference remains plausible and concrete in source, not proven audible cause: nxp-gain-analysis/DSP-SOURCE-RECEIPT.md and dsp-packet-receipt.json establish hostcalibrationIPI path and valid algo/excursion/MBDRC/model/filterbank packets. Their layouts/gains/active-mediaexecution are unresolved. Small libnxp_extamp_intf.so only initializesboardfirmware; that does not establish the rest of Android media chain absent. TheiOS implementation has no recoveredAndroidspeakerEQ/dynamicprocessing. Missingfrequencycompensation or changed interspeakerbalance could alter timbre; present data cannot distinguish those.

Current outputratio is deliberately unequal: top256/bottom2048 is+18.062dB digital ratio, top256/bottom4096 +24.082dB; physical sensitivities differ so neither defines acousticbalance. At4096 bottom still-18.062dB relativefullscale whiletop-42.144dB. A stronger/darker bottom relativetop could change combined timbre without any new formatdefect. This is a hypothesis, not an assertion about whichspeaker soundsmuffled.

## Highest-yield discrimination without another image

Use existingSTOP/idleGAIN/ENABLE and isolatedtone commands only as separately authorized byparent/user; no operationsperformedhere. Keep gains at alreadylistened levels first.

1. Ask which source/physicalspeaker soundsmuffled: replay same short MONO speech/music segment with top-only versusbottom-only usingzeroothergain. Use matched content and perceptuallycomparablelevel; stereoL/Rdifferentcontent is not a frequency-response comparison. Savedgainpair/DMApeaks/errors identifyactualpath.
2. Compare each isolatedspeaker at two alreadylistened gains, holdingcontentfixed. Stabletimbre but louderoutput favors scale/balance; level-dependent rasp/pumping requires separate report and cannotbe calledmufflingbyassumption. Parent can STOP promptly ifundesired.
3. Use existing isolated1kHztones to verifychannelidentity/clearsteadyoutput, but do not interpret them asEQsweeps. Bothspeakers may legitimately sounddifferent; a mono reference mattersmore than increasinggainagain.
4. If only combinedplayback is muffled, compare top-only/bottom-only/combined withsamecontent beforeassumingmissingDSP. Ifbothisolatedchannels consistently lackhighfrequencies, nextoffline target is AndroidmediaDSP/filterbank schema or measuredbandwidth, not a blindI2Swidth/ampfilterchange.

No concrete codepatch is justified by currentevidence. Existinglivecontrols can isolate balance/content/physicalresponse withoutreflash. Any proposed higher-usefulvolume still needs the protectionlimits distinction alreadydocumented; qualityreports do not establish a safemaximum.

## Refined after bottom isolation /5793 trial

Parent: user identifies bottom as muffled, no crackle;4096 volume nearlyright,5793(+3.011dB) trial pending. No inference of distortion or gain-dependent degradation follows yet.

Exact perfield inventory now saved `profile-filter-controls.json`, derived from pinneddevice+speaker operations. Both Androidboard and ourruntimeplan have identical HPFBYP0(reg51bit5), bypass_lp1(reg53bit5), FRACTDEL22(reg02bits5–10), LPM1MODE2(reg64bits14–15), LNMODE0(reg62bits14–15), TDMSRCMAP3(reg68bits0–2), sourceA1/sourceB2(reg68bits3–6), INPLEV0, AMPGAIN190. Eachmaskedoperation is readbackchecked atsuccessfulstartup. This establishes source parity of those masks, not an analogresponse measurement or behaviorofuntouched/autonomousfields. Thepublicfieldnames define no cutoffs/Q/lowpowermodetransferfunction here. LP1(reg6ebit1) is not programmed byselectedcontainer; do notinventitsresetmeaning.

AllsevenP/Qoperationdifferences remain exactly fourchannel/feedbackslot selections plus TDMSPKG4/5, DCMCC10/11, DCVOS63/55. TDMSPKG is sourceidentified as analoggain/volume, not a documentedfrequencyfilter. DCMCC/DCVOS are boostcurrent/secondboostvoltage controls; their effectonheadroom/dynamics does not prove a bottomlowpass. No bottom-specific fixedfilterdifference exists in thepinnedprogrammedmaskinventory. It is NOT proven thatthisprofileintentionallymakesbottomawoofer, nor thatbottomusesreceiverprofile: canonicaldecoder explicitlyselects` speaker ` forboth; bothcontainersdevicesalsohavereceiverprofiles, soexistenceofreceiverentriesdoesnotidentifytheactivepath. Mechanicalspeakerresponse, enclosure and missingAndroidEQ remain possible; designationwoofer/receiverresponse is unsupported.

Shortest useful existing-command comparison aftercurrent5793feedback: STOP→GAIN(top0,bottom5793)→ENABLE, replayoneshortexistingmono/spokenpassage; STOP→GAIN(top256,bottom0)→ENABLE, replaythesamepassage; STOP→restorepreviousdesiredpair→ENABLE. Keepalreadyauditionedlevels; compareclarity/timbre atroughlycomparableperceivedloudness, notmatcheddigitalgain. Confirmbottomalonefirst ratherthanchangingfilters. Ifonlyquestionisgain-dependentmuffling, comparebottomalone4096versus5793 withsamepassage andtop0, thenrestorepair. Parent/userauthorizeandexecutethese; noneperformedhere.

Important limitation: existingUSBGAIN onlymasks/scales channels; itcannotmixarbitrarymediaintomono orplaynewsweepfiles. Thefixedtoneisexactdualmono/isolatablebutonly1kHz. Useaverifiedexistingmonosource ifavailable; ordinarycenteredvoicevideomayapproximatebutdoesnotproveequalL/Rcontent. Withoutaverifiedmonosource thetonecanconfirmidentity/loudness, whilethemediaA/Bremainsqualitative. No newfiles/firmware needed for these bounded comparisons, and no blindfilterchange is supported.
