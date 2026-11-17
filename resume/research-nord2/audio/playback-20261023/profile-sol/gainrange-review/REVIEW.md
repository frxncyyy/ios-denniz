# Independent pending gainrange/startup review

Reviewed shared pending candidate after current NOTES active handoff. No shared-source/device/CI/commit edits. Private test binaries/logs/receipt only. Verdict: no concrete code blocker found in reviewed final startup/gain implementation; physical intermittent startup cause remains unidentified and candidate not hardware-tested.

Arithmetic: maxgain32768 with signed16 input and ramp<=480 has worst intermediate515396075520, safely int64. Signed division towardzero, then signed16 packing preserves exact input at unity afterfade, including-32768/+32767. Producer/downstream int32 product max1073741824 remains representable. Gain table absolute, bounded0..32768,256possible encodedpairs; defaulttop256/bottom1024 retains listening-tested pair. Zero channels/silence/ramp logic remains bounded. Increasing digital range is not a calibrated acoustic or protective limit and does not implement Android hostSpeakerBoost/thermal/excursion processing.

Protocol: all16nibbles accepted, highbits refused; CAPS14/15 append max/stepcount, CAPS12/13 retain operation/argument. CLI supports old0/0range as7steps1024, refuses newer requests beforemutation; new32768/16 advert accepted; range/boot changes abortpending without mutationretry. Legacy fixedtone remains256/256 through audio_device.c150–157; isolated top/bottom tones use configuredgain and can be much louder. Initial reviewer wording concern that legacytone itself reachedunity was incorrect; corrected after checking start(duration) override. Doc distinction is sufficient, not a blocker.

Startup: MANSCONF1 precedes PWDN0 (selected root Android tfa_dsp.c4307–4322 INIT_CF); AMPE1 after stable-clock samples (OPERATING4330–4345). Selected Android tfa98xx.c914–970 reads NOCLK AFTER tfa_dev_start. New port waits for two consecutive healthy samples in bounded101attempts perphase; tolerates historical pre-enableNOCLK but not missingCLKS/TDM; postenableclear thenpostwait requiresNOCLKclear too. Persistent pre/postfaults timeout with no queuedPCM, firstbad/laststatus saved throughcleanup; IOfaults terminate immediately. Runtime monitoring is unchanged and does not suppress subsequent faults. Time-based settling guard is a port policy, not a vendor-guaranteed100ms acousticspec. Cleanup still mutesboth and closesAFE. At worker level stickyerror blocks auto-restart, ENABLE, GAIN andtones; STOP does notclearstickyerror. No silent retry loop added.

Evidenceprintf reads only saved amp_start records, does not addMMIO. valid bits distinguish partialIO samples; firstbad and finalfailure remain separate. The previous real14thstart could have been timing/stickyclock/TDM; oldrecord lacked exactcause, so no claim that its cause is proven or fixedonhardware.

Focused actual-source verification (strictC11-Wall-Wextra-Werror, ASan+UBSan):
- balance:256gainpairs+16legacy, everypackedDMAword vsindependentoracle, extrema, ramp/zero/isolatedchannels/peaks, stop/restart/fault, invalidgains beforeMMIO; paired0c74 and exactboardprofile.
- restart-settle: temporalclock/TDM/post-AMPE historicalNOCLK eventuallyhealthy; persistentpreTDM/postTDM/NOCLK/clock refusal; immediateIO; immutablecausethroughcleanup;3clean0c74restarts; noPCM whenrefused.
- live: framing/token/ACKlossidentity, all65536encodings,256gainpairs, additivecaps/statusfields.
- PythonCLI compatibility and actualproducerpolicy extractiontests PASS (cli.log/producer.log).
Each compiledtest has sourcefileSHA256 and unchanged-during-run receipts in focused-tests.json. These models establish implementation behavior, not analog safety or normalAndroidvolume.

Independent image review next: clone run-balance.py only once final source/run are supplied, stageios-gainrange, unique gainrange-playback/rollback reportpaths; verify pinnedHAL, baselineADT/audioonlydelta, loader/source/kernel/payloadidentity andcapacity, preserve/re-hash knownworkingfallback. Do not run until parent confirms bothimagesready. No new hardware authorization exists.

Final source: `7ed29f2d5c8cf44d02eb50cdad4a133bb3011791`, CI40031037519 SUCCESS per parent. `final-source-receipt.json` independently compares tested source hashes with exact committed Git blobs: differences recorded; see receipt. Image review pending parent readiness notification.
The only hash difference was the subsequently expanded restart-settle test; implementation hashes matched. Recompiled and executed that exact final test with ASan+UBSan: PASS (`restart-settle-final.log`). All final implementation evidence now matches the frozen commit.
