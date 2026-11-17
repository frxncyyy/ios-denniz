# Private AP-wake diagnostic, after successful retained phase10

`wake.c/h` implement a one-shot separate wake/reference lifetime. `adapter.c/h` restrict access by phase and use a new always-on AP guard. No frozen/production persistent state is changed, no active integration/device/SMC/DRAM operation. This candidate is not approved for hardware. It can be considered only after successful phase10 hardware acceptance.

Order: software persistent-owner proof -> always-on AP checks -> HOST180601a0 initially0 (otherwise clean refusal) -> latch our reference before source write1 -> HOST18060184bit5->0 -> source readability clock measurement/IRQ -> CFG18001000 identity20010101 -> positive WFSYS/BGFSYS CPU-reset/top-off and WFSYS power-off snapshot -> source HOST1a0write0 -> no callbacks/MMIO whatsoever. No WFSYS power/reset write, SPI, semaphore, ROM, MPU or DRAM operation.

Guard before wake is **not** ncc_ap_check. Only always-on AP SPM10006304mask1f=d/ACK1000616c/170bit1, GALS1020e504bit0, APbusprotect10001228bit13 and10001724bit2 are checked. HOSTack need not be0 before the wake write. HOST1a0/184/clock/IRQ are the source ON-side entry registers. If post-HW-control AP power differs, this diagnostic cleanly refuses before any new write; it must never force those AP bits to make the check pass. No CFG/SYSRAM/AFE read occurs until new source wake ACK/clock/IRQ succeeds. Identity must match before RGU cold snapshots. Clock detector writes preserve the preceding value except source triggerbit0.

Bounds: ACK initial read then source-macro max11 reread/delay1000us; clock detector reset **each** retry, initial1us then10us, max1600 attempts/<16000us spent; IRQbit0 must1; ID10 reads (source platform has no actual delay despite its comment). Cold snapshot exactly18000010bit0/18000000bit7/180602ccbit2/18000014bit0/18000008bit7, all0. Deliberate fail-stop deviations: no source second ACK retry/20ms sleep or broad debug dumps after a timeout; no source random jitter (deterministic10us retry bound instead). These differences refuse earlier and need review, not a source-equivalence claim. A stalled MMIO is not interruptible by a software timeout.

`entered` prevents reentry, including after clean refusal or balanced success; optin other than1 invokes zero callbacks and does not mutate state. New reference goes0->1 before source wake might commit. Any read/write/proof/timeout uncertainty after that point retains reference/awake prerequisites and sets halt; no compensating sleep or speculative cleanup. Sleep is the final ordered command: reference clears only when its synchronous write reports completion; failure retains+halts. `balanced=1` means only **our** wake vote released, never CONN/common power restoration. The old npa retained lifetime and terminal2 remain unchanged on both success/failure. No old owner flag is repurposed or cleared.

## Native integration plan, not implemented here

- A new explicit opt-in, invoked only from the same sole input/radio owner after software phase10/handoff2/retained/no-fatal proof. Choose a separate object for this AP wake transition; do not reopen, memset or call old npa/npc callbacks.
- Backend `owner` must inspect **software only**: old phase10 complete, terminal2, outer retained latch, exclusive AP-wake transition/serialization, no other host1a0 voter. It cannot call ncc_ap_check (that includes HOST184) or source off-domain predicates before the wake.
- Use already mapped AP SPM/infra/GALS and source ON HOST aperture to implement raw32 with ni_rd/ni_wr plus ordered barrier. CFG/RGU raw mappings may be reused only behind this adapter's post-wake gates. Mapping an aperture does not bypass those gates.
- New core/adapter are separate translation units; no calls into old terminal-state adapter. A new explicit driver owner/refcount is separate from the source common power lifetime. One-shot refusal on nonzero HOST1a0 avoids stealing an unknown vote; generalized shared AP consumers would require a serialized shared refcount API before relaxing this guard.
- After success emit software-only result (identity/raw cold masks, balanced1/ref0), and perform no post-sleep register report or readability check. Other normal AP input operations remain separate; radio off-domain work later needs another reviewed wake acquisition.
- On halt route directly to existing manual-reset panic path, honoring old persistent-lifetime cleanup refusal. Do not let failure call nw_restore or old common restoration; never modify persistent terminal2 just to pass access checks.

Sources pinned04d00c25: consys_hw.c:563–610 (0->1/1->0 API reference rules), mt6893_pos.c:468–565 (HOST1a0/ACK/identity/sleep), mt6893_consys_reg.c:533–615 (AP/GALS/clock+IRQ/readability), consys_reg_util.h:90 (actual count>loop poll behavior). WLAN HOST1a4 is a distinct vote and is not used here.

Validation: C11 Wall/Wextra/Werror plus ASan/UBSan PASS; every240 backend callback failure in successful path stops without another callback or sleep; default-off/reentry/unknown vote, ACK11/clock1600/ID10 timeouts, IRQ failure and non-cold state covered. Backend asserts no off-domain read before awake proof/identity and no callback at all after source sleep. Both translation units ARM64-compile against39398655023 generated headers. Mock/compile evidence is not hardware wake acceptance or current MPU permission.

Exact source links:

- [Wake references]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/consys_hw.c:563)
- [Platform wake/sleep]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:468)
- [Readability]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_consys_reg.c:533)
- [Poll macro]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/include/consys_reg_util.h:90)
