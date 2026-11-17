# Final native / worker / GIC / reset read-only review

Reviewed current canonical after producer integration and concurrent parenthardware
fixes, plus actual buildtree `pexpert/arm/pe_fiq.c`. No further blocker found in
this bounded pass. Generatedpatch regeneration/sourcefreeze/nativecompile remain
parent gates, not inferred from this read-only result.

* PEhelper definition is present at pe_fiq.c56-65 under NORD2. It checksvalue/base,
  reads existinggicd_base+0x110 only, makes no mapping, interruptwrite, redistributor
  access or allocation. DTS i2c8SPI120 (mt6893.dts6913) becomesINTID152, ISENABLER4
  bit24. Actualnative delegatesonlyexactPA0c000110/width4 onowningworker withDSB
  before/after; differentwidth/address/worker rejected; no tenthdevicealias.
* GIC enable tested beforeoldIRQacknowledgement andagainimmediatelybeforeSTART.
  AP peripheralmask0x129 matchesselectedV2multichannelcomp/arb/timeout/buserror;
  read-onlyGICcheck requiresCPUdeliverymasked. No undocumentedINTR_MASK0latching
  assumption or bypass ofactiveIRQowner. CleancompletionnowW1Creadbackverified
  before restoringoldmask. Saved15setupfields allrestoreonlyoncleanpaths.
* I2CR is a distinctcoldprimitive, notordinaryreg00RMW: freshsameamp SYS0 evidence
  demandsPWDN1, I2CR0, AMPE0; literal2 once; waitselfclear bounded100ms, neverresend.
  Runtime/profile writescarryingI2CRbitreject. Thenexactsiliconinit andexactpinned
  board volatilekeypreamble,52bitfields,lock/rate/clockchecks. NoMTPcommand/data.
* Normalstop nowclearsAMPEbothamps before firstperamppowerdown/statewait; failed
  unverifiedmute takeslocalresetroute beforedigitalclose. Selectedstopwait20x5ms
  isexpressed inbounded1msnativecallbacks andfinalread. Fataldeadline/pointer/stall
  assertsownedGPIOreset beforeanyI2Cwait, closesownedAFE, latchesuncertainty and
  preservesreset/clockevidence; noautomaticrestart afterownershiplost.
* Producerhelper bounded8192frame memoryscan only, producerlockheldonlyforpurequeue
  state. Ringdrain timestamp updatesAFTERtickwrites/monitor. HALzerotails do not
  preventquietstop, stalevalidqueuedPCM ispreserved, NAL_ENABLEstickyerrorrefused.
  No synchronousnormalPIOprinting whileaudio isrunning. NohardRTbudget applied
  aroundcoldinit orI2Cpolls.

Ran finalcanonical durable `test_producer_policy.py`: ASan/UBSanPASS actualqueue,
extractedcommands/downstreamhelper, longstartup,ringdrain,zerotails,wrap,counters,
mute/disable,stickyerrors. Ran actualnative `test_ready.py`: all9publication/wait/
errorcases andsaveddiagnosticsPASS; verifiesexactGICdelegation/width/owner/no-write.

Minimal obsolete-idleassert update is `test-idle-policy.patch`, actualcanonical
stream ASan/UBSanPASS. Newtestpreservesoldmeaningfulclick, validatesexactdequeue,
thenflushesfreshzero-onlytail withoutrefreshinglast_sound. Parentapplies it.

Limitations unchanged: noactualampREV/completion/DMAprogress hardwareproof; ISA/image
review occurs only againstfinalsourceCI artifacts. Parentpreservesnativebuild/whole
suite/sourcepatchmatch requirements before another independentlyreviewedimage.
