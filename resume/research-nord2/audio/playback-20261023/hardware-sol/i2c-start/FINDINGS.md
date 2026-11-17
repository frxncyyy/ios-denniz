# I2Cv2 AP START=2 audit

Measured clockfix7d2ca8f9: init=-3, gate0, own800651ff, stage8/failure6, PA11d04124,value2. This is AP START (11d04000+channel100+offset24), before any transport programming. Prior stage8 checks already passed shadowSTART, CCUSTART, shadow/CCU CONTROL DMA, shadow/CCU FIFO, and APDMA EN when clocks permit. AP CONTROL/IRQ/FIFO/I3C checks occur AFTER this refusal, so they remain unknown, as do amp revisions and POR state. No controller failure has yet been demonstrated.

## Source-exact interpretation

Selected Android04d00c2 `drivers/i2c/busses/i2c-mtk.h:55-56`: bit0 I2C_TRANSAC_START, bit1 I2C_RESUME_ARBIT. `i2c-mtk.c:403-427` mt_i2c_wait_done checks ONLY bit0 to decide transfer-in-progress before clock-gating. Normal transfer begins with literal I2C_TRANSAC_START=1 at:1211-1213, with no clearing or rejection of bit1. Thus treating inherited AP value2 as busy conflates status/command meanings and contradicts the selected driver's busy predicate.

RESUME_ARBIT is an ACTION when written. Android timeout and serious-error recovery writes literal2 to the SHADOW channel only (:1253-1254,:1334-1337), after reset/init_hw. Do not copy that recovery or write2 anywhere. The narrow correction is acceptance of AP readback0 or2 while preserving bit0 busy. Literal START1 is the only allowed START write and only on leased AP channel; no zero/2 writes, no reset, no shadow/CCU mutation.

## All AP predicates must agree

Current audio_i2c8.c tests AP START with mask3 in four places: initial preflight; adoption of exact prior completionIRQ1; successful completionIRQ1; clean multi-channel completion+ACKERR IRQ3. Correct all four to source transaction-busy bit0 (or exact supported0/2 check). Fixing initial preflight alone causes next transfer to become uncertain if hardware preserves bit1 through normal completion. Keep initial shadow/CCU restrictions unchanged as requested. Preserve all DMA, FIFO, I3C, pad, clock and CAM guards.

## Complete normal transfer/restore audit

- Source timing/control: c963-1008 sets HWtimeout and clockdivider, chooses ACKERR_DET|CLK_EXT control28 for100k writes and DIR_CHANGE|RS additional12 for WRRD3a; c1011-1055 startcondition/delay/open-drain/timing; current port matches selected100k PIO mode.
- Source c1067-1079 clears IRQ then multi-channel FIFO5; port only acknowledges exact prior ownable completion1 after all idle checks. Broader inherited error statuses remain refused deliberately; do not wholesale clear unknown errors to get past startup.
- Source c1202-1213 enables MCU_INTR then barrier and START1. Current native callbacks provide completion barriers and only AP channel is programmed. INTR_MASK0 polling is deliberate; source IRQ handler c1697-1701 masks then reads/clears latched status, supporting status availability independent of interrupt assertion.
- Source normal completion c1343-1353 reads exactly requested PIO bytes. Port checks exactIRQ1 before two-byte read and then clears ownIRQ1/FIFO5. Clean IRQ3 NACK handling matches source c1326-1330, returning remote error without reset or arbitration action.
- Current port restores its15 saved AP config words in reverse order; this is a port ownership feature beyond Android (which keeps transfer configuration). START, FIFOcommands and W1CIRQ are not snapshot-restorable configuration. Never restore inherited START2 by writing2. Poll status/timing and restoration failures remain uncertain; no blind retry.

## Next unmeasured guards / blockers

AP CONTROL DMA bit2, prior IRQ not0/1, FIFO_STAT not0 and I3C_DMA_FSM_DEBUG bit15 can still cause a legitimate safe refusal. They are not evidence for relaxation. APDMA/shadow/CCU checks preceding measured START2 already passed on this exact attempt. A future first-transfer timeout/error must report AP completionIRQ plus START/ERROR/FIFO/stage without resetting shared hardware. TFA9874 boardprofile match and actual postreset powerdown remain unknown until transfers succeed; no basis to claim audio ready yet.

## Meaningful regression plan

1. Model inherited AP START2 as persistent/readable through launch and successful completion: START1 write sets busybit0 while retainingbit1; completion clears ONLYbit0, yielding2. Assert read16, write16, NACK, repeated read and entire3-cycle playback succeed; normal AP config restored, no write2/0 to START.
2. Exact prior completionIRQ1 with AP START2 must be adopted after duplicate idle checks via one W1C1; then normal transfer succeeds. StickyIRQ and newly appearing busybit0 remain rejected/uncertain as appropriate.
3. Initial AP START1/3 refuses before writes; bit0 appearing at adoption reread refuses; bit0 stuck after own completion becomes uncertain. Preserve independently busy shadow/CCU, DMAenabled/CONTROL_DMA, FIFO/I3C and rail51/56 refusal tests.
4. Assert zero controller-reset writes and zero shadow/CCU/RESUME actions across every case. Update both independent lifecycle and transport models; do not merely change fixture START2 to0.

No canonical, device, CI or buildtree edits. Parent owns proposed implementation.
