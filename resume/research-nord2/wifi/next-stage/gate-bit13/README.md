# Private source-aligned persistent guard + optional scopedID candidate

Current hardware: LIVE x2 proved phase5/60commands/18SETs/2reads independently; common skipped with ownerall1ff, valid1f, failed4. Rawprot e70c500e has bit14=1, bit13=0; rx/host rawzero UNREAD. Outer stage3ID20010101cfg3 success1/restored1. No evidence any40commonregister was written. No source/device/CI/UFS edits here.

## Minimal change: EXACTone mask

`wifi_common_adapter.candidate.inc` differs from current canonical adapter ONLY:

```
NCC_AP(4,nw.infra,0x228,0x6000,0);
->
NCC_AP(4,nw.infra,0x228,0x2000,0);
```

This is a persistent **AP2CONN readability** predicate, not removal of all bus protection. Selected MT6893 active consys_check_reg_readable (`mt6893_consys_reg.c:548–566`) checks RX1724bit2clear and TX1228bit13clear; it omitsbit14. POS (`mt6893_pos.c:279–294`) explicitly disablesbit14 ACK0 poll under `#if0 /* POS update20190819: Skip check */`, then still pollsbit13zero. Selected clockpowerdriver ignoresreleaseackchecks. Bit14 is CONN2AP RXack, distinctfrom AP2CONN TXbit13 and AP2CONN RX1724bit2. This new hardware confirms exactly why copying bothcold-setupackbits into a persistentreadability gate blocks progress. It does NOT establish requestbit14 remainsset; 1228STA1 is ACK, not1220PROTECTENrequest.

Allother6APchecks, freshoriginalcoldowner, power/rails/PMIC, first13coldclientfields+SPIidle, dirtyhalt, read/write/restoreallowlists and APrawobservability stayunchanged. Outerpower still clears4000+2000 and performs original one-time nw_wait mask6000zero before6ms/ID; no powersequence/wait/ni_rd/ni_mask/barrier change. Currentwait equality is correct: source/logprove some earlier clearobservation, not causalreason forlater14high. NoTOPforcewake orDDR_ENbypass added.

`test.py` extracts andcompiles ACTUALcandidateowner/APguard underASan/UBSan. Actualcapturedpwr=d/acks10580e03/gals1/protE70c500e nowpasses; bit13set,6000,PROT2bit2,HOSTbit5, power/acks/clock/owner failures stillreject withunchangedfirstfail valid/raw shape. It asserts fullcandidate isexact1maskdiff and originalouter6000wait remains. Fullprivate sweet_touch candidate BSDARM64compilePASS.

## Optional same-image scopedID candidate

`scoped-id-candidate/` stages the independently reviewed retainedcommon/strap/ID machinery against thismask, with one concrete missing dependency closed below. It is NOTlinked orhardwaretested. Separate nord2wifiadieid=1 requires existingwifi2/common1; optout preserves commonobserver. NoUFS/boot behavior changes.

The callback executes onlywhile all40sourcecommon values areapplied+verified. All4GPIO180DIR/DOUT/MODE andADIEreset snapshots aretaken beforecommonmutation. Source26MHzstrap sequence output/high/muxGPIO thenADIEresetdeassert; resetmustoriginallybeasserted. TOPSPI CHIP_ID READ (notanalogwrite) semaphore11, commandb02c, boundedbusy100poll/phase; known66358a00/10/11 accepted. UnknownID cleansupbutdoesnotprogress calibration. Oncleancompletion: restoreTOP_ADDR/releaseownsemaphore -> restoreADIEreset/mux/data/direction -> reversecommon40 -> existingouterpowerrestore. DirtySPIcompletion or anyrestorefailure retainsallownedstate and existinglatch/WFE manual-resetcontract; noouterpoweroff/Linuxhandoff.

**Readability dependency:** sevenAPchecks are NOT thecompleteAndroidreadability algorithm. Source semaphoreacquire (`mt6893_pos.c:1877–1885`) invokesreadability (andbus-hangfallback). Actualreadability also resets/pollsHOSTclockdetector andcheckstimeoutIRQ. The previous privateIDguard omittedthat. Newprivate `nis_clock_probe` runs afterstrap/reset but BEFOREfirstsemaphoreacquisitionread:

- source-preserving SET_BIT on18060000bit0 (clockdetectmeasurementreset);
- boundedpoll bits1and2==1 foroffbus/oscclock, firstdelay1us then10us, max1600iterations(~16msdelaymax; sourcemax_wait16000 despite1.6mscomment);
- read180602d4bit0, require1 (nooffbus-hang indication).

Ifclocks/IRQfail, doNOTreadsemaphoreorissueTOPSPI; restorestrap/common cleanly andreportADIECLOCKattempt/ok/raw/IRQ/polls. Itdoesnotenableclock, forcewake, modifycalibration orrelaxfaultchecks. Sourceclockprobe resetsmeasurementstate; it is not a persistentconfiguration word thatcanbe restored byreplayingoldstatusbits. This omissionofmeasurementhistoryrestoration isexplicit, justasTOPSPIreadRDdata cannotberecreated. NativeMMIOcanstall: finitepollcounts doNOTmakeanunreturnedbusreadtimeoutrecoverable. Clockprobe bounds are deliberatelyfinite andremoveLinuxrandomjitter/debugdumps; theymayconservativelyrefuseaslowdevice.

Combined hosttests useACTUALnewnativeadapter withprotE70c500e throughout. Verify40values+strapremainappliedatexactSPItrigger, noearlycommonrestore, all91faultablecontrolwritepositions of92controlwrites (semaphorerelease hasno recoverableerrno/readbackcontract), semaphoretimeout, unknownID, ambiguousSPI/guardloss retainsstate, plusclockdetecttimeout/IRQzero ->notrigger+cleanreverse. Clockmeasurementtrigger is testedseparately, notcountedasa restorablecontrolwrite. FullcombinedcandidateBSDARM64compilePASS.

## Recommendation/risk

Integrate sourcealignedmaskfix. Same-next-image conditionalID isreasonable **after independentreview of this addedclockprobe and regeneratedcandidate**; leaveIDoptoutifnotreviewed. Thisavoidsanotherobserver-onlyboot whilekeepingexplicitconditionalgates. NoRFpackettraffic/CPUrelease/ROMplacement/calibration permitted. SourceSYSRAMhandoff/16KiBclear intentionallyomitted: thisisIDdiagnostic, notcompletea_die_cfg. EMIpermission ambiguity isirrelevantto theseMMIOstages andremainsabarriertoROMplacement/execution.

FutureAPlogparser mustuse mask2000 forpersistentfield4, otherwise acapture failinglaterfield5/6 withbit14high willbefalselyrejected. Tagnewsourcepolicy explicitly; doNOTreinterpretoldmask6000captureasifnewsourcehadfailed.

Source:
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_consys_reg.c#L533
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c#L272
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c#L1857
