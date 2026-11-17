# Independent inherited-I3C / native-binding audit

Actual `ios-i2c/hardware-review/init.log` AP evidence is valid1f, START2,
CONTROL0x28, IRQ1, FIFO0, DMA_FSM_DEBUG0x8800. STARTbit0 is idle and the single
old-completionW1C adoption path is applicable. Bit15 is read-only inherited I3C
capability/configuration; selected Android `drivers/i2c/busses/i2c-mtk.c:1040-1047`
uses it to add HFIFO master-code setup only when speed_hz>MAX_FS_PLUS_MODE_SPEED
AND !hs_only. Fixed100kHz path does neither. No debug/HFIFO/control-mode register
should be cleared to make that predicate pass. Existing FIFO/shadow/CCU/DMA/idle
checks remain independent prerequisites.

Verified actual canonical native bridge + transport + silicon tables together in
private `test-binding.c`, compiled with strict warnings and ASan/UBSan:

* Actual native nine-window resolution accepts AP setup15field offsets with16bit
  accesses, byte FIFO payloadwrites andbytereads, START16bit and IRQW1C16bit.
  Setup/restore accesses use exactly the same allowedregisters and widths.
* Native read correctly returns0x8800 fromAP+0xec. Native writes reject this
  readonlydebug, HFIFO+0xf0, shadowSTART, CCUSTART, and APDMAenable.
* APDMA idle read11000788 resolves in retained11000000 window; no second mapping.
* Every register/value in actual ra74,rb74,rc74 silicon tables passes actual
  transport exact-value allowlist. All0a74/0b74/0c74 classifySUPPORTED.
  REV03,system00,status14,clock11,keyFB reads are accepted; wrongslave35 and
  MTPdataF0 rejected. BoardprofileadditionalRMW operations are dynamically
  allowed only after pinnedboardload (covered by existing104oploader test).

Selected source confirms FIFOdata uses byte operations, not16bit assumptions:
_i2c_writeb/readb at41-49; transmit1196; receive1350. Thus native FIFO width1 is
intentional. Source registerconfiguration uses16bitreadw/writew; native matches.

Restore review: only15 saved APsetupfields restored, inreverseorder, after clean
completion/NACK; START/IRQ/FIFOclear are commands and not replayedfromsnapshot.
No shadowRESUME_ARBIT write or APDMA reset follows uncertain transfer. Original
STARTbit1 readback is not restored: normal sourceSTART1 is a freshtransaction,
while shadowSTART2 belongs solely to errorrecovery. Singlecompletionadoption is
recheckedbeforeW1C. The planned100kguardcorrection does not alter thesepaths.

No further concrete nativeallowlist or supported9874-register blocker found.
Physical ampREV and actualcontroller completion/data remain unmeasured; a successful
hostbinding test is not proof of electrical100k transfer or correctsilicon onphone.
No canonical, CI or device mutations performed. TestPASS output retained in chat;
parenthardwareagent owns changedguard and actualtransport regression fixture.
