# Selected source flags and persistent rail ordering, v2

Version1 native files and frozen phase10 core are unchanged. This new private whole-source copy changes only selected buildword0->3 and adds persistent-only rail sequencing. No active source, CI, image or hardware changes.

`conninfra/Kbuild:115` unconditionally defines CONNINFRA_PLAT_BUILD_MODE=3. This was missed by the earlier search limited to Makefile/Android.mk; the v1 absence assumption is superseded. Android's saved post-probe-dmesg.txt:1875 independently observes the D-die buildword write3. V2 now passes3 through the frozen core API, preserving the write before the subsequent source A-die SYSRAM clear. Core implementation was already parameterized/tested for0..3, and no core change is needed.

Kbuild:14 defines MTK_CONNINFRA_CLOCK_BUFFER_API_AVAILABLE=1. Selected saved config identifies4.19/CONFIG_MTK_PLATFORM=mt6885, while connectivity selection routes6893; do not infer chip from the platform string alone. mt6893.c:250–267 explains that clockbuffer is HW-controlled and this API updates status only on this selected older-kernel path. Native candidate reuses the proven outer direct power/6msXOsettle sequence; full clock/power-driver correspondence remains an independent review item, not a new TCXO mux write.

Saved extracted `/vendor/firmware/conninfra.cfg` is32bytes SHA25647985608830ebb4038ea7b8645e5244dabe848f5033b3493b73a5ea9467993d9 (manifest ties it to saved vendor image). Its only assignments are `co_clock_flag=1` and `pre_cal_mode=3`; there is no tcxo_gpio. conninfra_conf_init zeroes the entire configuration before parsing; CHAR(tcxo_gpio) is registered at162 and no parser assignment is present. Therefore this **saved configuration plus selected source** yields tcxo_gpio0, and mt6893.c:282–285 returns26M_COTMS. Both source branches are26MHz; 52MHz is unreachable in this selected clock function. GPIO155 TCXO branch in pos.c:636 must not run for this policy. This is offline config/source attribution, not runtime verification of an arbitrary future Android override. The iOS private candidate intentionally pins the recorded26MHz/non-TCXO policy and does not load/guess a new config.

Selected4.19 RC initial rails configure VCN18 then VCN13: OP_EN bits7..4, OP_CFG bits7..4, SW_LP bit1, fixed/validated voltage, then SW_EN bit0. Original six-slot diagnostic combines LP|EN bits1|0. V2 preserves OP field helper/order but separates the two CON0 transitions **only when nord2wifipersist=1**. New fixedslot helper accepts2 or5 only, requires lowbits0 then2 and unchanged other fields, writes2 then3 through the original serialized PMIF command/wait/readback protocol. VCN18 is fixed1.8V; original VCN13 VOSEL/VOCAL gate proves1.3V. No voltage write is introduced. Each step validates complete readback; failures before retained CONN/register initialization use existing outer safe cleanup; failures once retained still cannot teardown.

Exact extracted helper tests prove bothrail order2->3 and immediate stop on each of12 callback failures, wrong initialEN/unownedfields and owner/slot refusal. The original nw_pmic_write function body is byte-identical to v1; the loop fallback still calls it for every slot in diagnostic mode. Whole private v2 source ARM64 compile PASS with same generated headers/flags as v1. V2 still references frozenv1 adapter/inc (under review); any adapter review fixes will be separately versioned, never silently mutate frozen files.

Current legacy MPU read error-1 establishes no metadata or effective permission. Neither v2 change accesses DRAM/MPU, changes historical address provenance policy, releases WFSYS/BGFSYS CPUs or authorizes hardware.

Source links:

- [Kbuild flags]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/Kbuild:14)
- [Buildword3 definition]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/Kbuild:115)
- [Observed Android buildword3]($NORD2_REPO/backup/nord2/post-probe-dmesg.txt:1875)
- [Zero-init parser]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/conf/conninfra_conf.c:558)
- [Registered TCXO field]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/conf/conninfra_conf.c:162)
- [Clock selection]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893.c:271)
- [TCXO GPIO155 branch]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:630)
- [RC rails source]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pmic.c:357)
- [Saved config]($NORD2_REPO/research/nord2/wifi/firmware/conninfra.cfg:1)
