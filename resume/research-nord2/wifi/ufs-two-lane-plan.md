# UFS G4 x1 -> x2 audit (2026-10-20; no implementation/device writes)

Evidence: current XNU DME_GET reports RXgear4/TXgear4, activeRX1/TX1,
HSseries2(rateB), power0x11(FAST/FAST), six valid. Saved Android reported
G4x2B; it does not prove LK's inherited lane1 PHY/power state is presently
ready. Selected local Linux source ../nord2-current-kernel commit04d00c25.

## Smallest next diagnostic (local GET only)

Under the existing UFS mutex, after command doorbells are empty and UIC ready,
read each with independent valid/result/deadline, not aggregate zero defaults:
- PA_AVAILTXDATALANES0x1520, PA_AVAILRXDATALANES0x1540;
- PA_CONNECTEDTXDATALANES0x1561, PA_CONNECTEDRXDATALANES0x1581;
- repeat active0x1560/1580, gear0x1568/1583, HSSERIES0x156a, PWRMODE0x1571;
- TX/RXtermination0x1569/1584, PA_LOCALVERINFO0x15a9,
  PA_MAXRXHSGEAR0x1587, PA_TXHSADAPTTYPE0x15d4 (where supported).
Snapshot HCE/HCS/IS/AHIT0x18 and all UIC error registers without clearing
unrelated W1C bits. Existing root code serializes I/O but does not own Linux
clock/PHY power management, so avoid importing a generic Linux resume path.
A missing/unsupported adaptation GET must be marked invalid, not written.
No PEER_GET needed for first diagnostic: selected MTK negotiation uses local
connected RX/TX lanes to choose lane counts. If either connected/available
count is<2, do not attempt x2. Connected2 still does not prove active lane1
PHY can be enabled without additional vendor setup.

Peer device capability/active/gear GETs would strengthen host/device agreement
(localRX pairs peerTX, localTX pairs peerRX), but add DME_PEER_GET support and
must remain separately bounded. Generic ufshcd.c:4059 can temporarily change
FAST->FASTAUTO for peer access if DME_PEER_ACCESS_AUTO_MODE quirk is set;
that wrapper is not read-only. This selected MTK driver does not set the
QCOM-specific DELAY_BEFORE_DME_CMDS/BROKEN_PA_RXHSUNTERMCAP/
DME_PEER_ACCESS_AUTO_MODE quirks (ufs-qcom.c:985ff). Do not copy Qualcomm
quirks just because current XNU UFS code originated on sweet.

## Actual Linux lane/power protocol

ufshcd.c:4390 reads local connectedRX/TX, local maxRXgear and peer maxRXgear
for TX limit. ufs-mediatek.h:40–50 limitsRX/TX2, gearsG4, rateB. PRE_CHANGE
ufs-mediatek.c:1462 computes host/device intersection. Controller version
comes from PA_LOCALVERINFO>=UniPro1.8(encoded5) ->major3 (:929). For major3
and G4 it DME_SETs PA_TXHSADAPTTYPE0x15d4=PA_INITIAL_ADAPT1 (othergears0).
MT6893 does not take the MT6877 exception that skips this write.

ufshcd_change_power_mode(:4449) runs on the ACTIVE link; it does **not**
explicitly enter/exit HIBERN8 for lane changes. It sets in order RXgear,
activeRXlanes, RXtermination, TXgear, activeTXlanes, TXtermination, HSseries;
then PA_PWRMODEUSERDATA0..5 and DME_LocalFC0/TC0/AFC0 timeouts; finally
DME_SET PA_PWRMODE0x1571=(pwrRX<<4)|pwrTX. Both remainFAST ->0x11.
The PA_PWRMODE write triggers negotiated link transition even if requested
power/gear/rate values are unchanged; it cannot be replaced by writing the
two ACTIVE attrs and assuming those GET values prove a completed transition.
Generic code ignores individual preparatory SET failures; XNU must check all.

ufshcd_uic_pwr_ctrl(:4125) is distinct from ordinary UIC SET completion:
serialize UIC engine; wait for UIC_POWER_MODE ISbit0x10 asynchronous completion
(ordinary UIC_COMMAND_COMPLbit0x400 alone insufficient); check HCSUPMCRS
bits10:8==PWR_LOCAL1, and command/result readiness/error. PWR_OK0 is treated
as failure here; remote2/busy3/capability4/fatal5 are not success. Kernel
uses500ms timeout. XNU must have both absolute deadline+iteration cap and
handle W1C only for its own stale/new completion flags, no lost fatal errors.
Snapshot/restore interrupt mask or avoid changing IE in polling implementation.
Read back actual gear/lane/termination/rate/power and errors after completion.

## Conditional candidate transition, not presently ready to implement

First prove both connected+available directions2, controller version/adapt
policy, healthy current link and known recovery. Perform before writable root
I/O begins, or with all I/O drained/quiesced under the UFS mutex. Never alter
lane state while a root read/write/firmware reader is in flight. Preserve
root binding/LUN/transfer descriptors. Do not hold input/PMIF locks.

Snapshot all touched PA attributes and adaptation/timeouts, AHIT and IE.
If AHIT enabled, a safe transition must first disable AHIT and wait host idle;
MTK broken-AH8 quirk is enabled for this compatible, and mt6893 additionally
VCC_ALWAYS_ON/HCI_PERF_HEURISTIC(ufs-mediatek.c:85–94). Its AH8 disable path
writes debugselector0x22c0=0x20 then polls probe0x22c8 low5bits: states8..13
are H8-enter/confirm, idle1,5ms bound (:1400–1460). XNU current mapped register
window may not include0x22c8. Do not assume AHIT=0 alone proves ACTIVE. If
already H8, explicitly EXIT with UIC_HIBERNATE_EXITbit0x20 completion/UPMCRS,
not forced enter+exit around the power-mode change. New H8 support itself is
an additional prerequisite, so diagnostic should report inherited AHIT first.

Minimal state-preserving experiment would change only activeRX/TX to2 plus
requiredG4 INITIAL_ADAPT policy, preserve existing gears/terminations/rate and
validated power timeout settings, then issuePA_PWRMODE0x11 and verify. Linux
reprograms all listed parameters; diverging to fewer SETs is a candidate, not
proven safe until preserved timeout values/required attributes are established.
An exact Linux-style experiment could reissue unchanged snapshot values in
Linux order and separately snapshot/restore timeout/adapt fields. It must
verify each SET before the final transition and have an explicit total bound.

Rollback before final PA_PWRMODE: restore accepted staged attributes/adapt
with bounded SETs and verify original state. After any potentially executed
power request, raw attribute rollback is insufficient: if link still healthy,
request original lanes1/gear4/rateB/FAST using same negotiated protocol, verify
completion+all readbacks, then restore extra staged fields/AHIT. If UIC fatal,
link lost, timeout with ambiguous completion, or rollback fails: stop storage
I/O, do not resume writes or pretend x1 restored. Linux uses full device/
host reset-and-restore recovery (:4226), which XNU currently lacks. A hard
reset fallback cannot be assumed to recover a mounted writable filesystem.
This absence is a material blocker to a safe live-root transition today.

MTK PHY/clocks: pwr_change_notify has no lane-specific analog/clock writes;
setup_clocks/mphy_power_on are probe/resume/gating paths (ufs-mediatek.c:886,
1239,1357), not invoked per lane change. Shared PHY analog bringup manipulates
PLL/CDR/squelch/DIFZ but contains no explicit lane1 enable protocol in
phy-mtk-ufs.c. Therefore do not invent lane1 MMIO controls. Preserve inherited
clocks/PLL/refclk and board DT assumptions; successful Linux G4x2B supports
physical capability but does not prove omitted LK lane1 setup is unnecessary.

## Test/result criteria and realistic performance

Host model must fault each preparatory command, async completion/result,
stale interrupt, invalidavailable/connected counts, concurrent/outstanding IO,
AH8 transition, readback mismatch, and rollback step; no writes on gate failure.
First hardware experiment should use a protected read-only context with known
recovery, transition2->verify/read known data->rollback1->verify original data.
Success requires controller and peer agreements where queryable, no new UIC
errors, exact digest read, original state restored; throughput is separate.

Profile command wait9.76s in93.68s wall. Even ideal doubling of every command
interval saves4.88s(~5.2% wall) ->88.8s; real fixed latency/CPU/GUI means less.
Eliminating all measured command time entirely would only save~10.4%. These
are optimistic arithmetic bounds under the same workload, not predictions or
measured speedups. Queueing/polling/framebuffer timing may dominate wall.
