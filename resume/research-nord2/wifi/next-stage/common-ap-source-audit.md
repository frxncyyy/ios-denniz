# Seven AP predicates: source-only audit after common skipped

Current hardware capture: `research/nord2/ufs-linereset-common/ios/hardware-live/xnu-live.log:716–719` says common `skipped=1 reason=owner-or-AP`, outer power stage3 ID20010101 cfg3 success1 and restored1. There is NO old-capture ownerfail/valid/raw/failed breakdown. Thus no particular predicate failure is established. Four-file verified status0 earlier is independent and does not establish powered-CONN guards.

**Finding:** no demonstrable address/mask/polarity mismatch in these seven AP predicates relative to the selected MT6893 power path and our outer sequence. One real inconsistency exists in selected POS source: writes0x2 to PROTECTEN2_CLR while polling bit2. Selected clk-mt6885-pg power driver uses0x4 consistently, and our outer/helper ALREADY use0x4. Do not “fix” the helper to0x2. New precise observability is appropriate; no gate weakening is justified by the old capture.

Selected Android revision04d00c25, current outer `bsd/dev/sweet_touch.c:584–670`; current canonical `shim/nord2-xnu/wifi_common_adapter.inc:35–59`.

|AP index|Physical register/mask/want|Exact source meaning|Outer relationship|
|---|---|---|---|
|0|10006304 &1f ==0d|PWR_RST_Bbit0=1;ISObit1=0;PWR_ONbit2=1;PWR_ON_2NDbit3=1;CLK_DISbit4=0. clk-mt6885-pg.c267–271,1377–1401; mt6893_pos.c164–242|Outer sets the same5bits and checks fullmask immediately before mapping/ID; no sourcepolarity mismatch|
|1|1000616c &2 ==2|primary CONN powerackbit1; clk driver CONN_PWR_STA_MASKbit1, mt6893_pos.c174–188|Outer waits exactbit1 before deisolating CONN; helper rechecks after6ms/ID|
|2|10006170 &2 ==2|secondary CONN powerackbit1; same source195–213|Same. This is CONN domainack, NOT WFSYS/BGF status|
|3|1020e504 &1 ==1|INFRA_AP2MD_GALS_CTL bit0 enables AP2CONN hostclockECO; mt6893_pos.c148–153|Outer setsbit0 and checks it immediately before ID. Name AP2MD does not make this the wrongregister: selectedsource explicitly uses it for AP2CONN|
|4|10001228 &6000 ==0|PROTECTEN_STA1:bit13+14 CONN busprotect acks. clk driver291–294,208–211,1403–1422|Outer clears4000 then2000 via2a4 and waitsbothzero. Androidpowerdriver deliberately skips releaseackchecks, POS disablesbit14check but keepsbit13check; our stricterzero requirement matches existing successfulouterguard. Sourceomission ofackcheck does not prove zero is semanticallywrong|
|5|10001724 &4 ==0|PROTECTEN_STA1_2 bit2 CONN protectack. clk driver295–296,232–235,1410–1416|Outer clears4 via718/waits724bit2zero. POSwrite2 is internallyinconsistent with itsownbit2poll; not applicable correction|
|6|18060184 &20 ==0|HOSTCSR conn_infra ON2OFF sleep-protect ACKbit5, readablewhen0. mt6893_pos.c487–496; mt6893_consys_reg.c609–615 (active check; earlier516–520 copy is disabled)|Outer nw_wait checks the same bit before ID; helper rechecks it. Do NOT replace with OFF2ON bit3, whose source name/role is different|

All AP bases match source headers: SPM10006000, INFRACFGAO10001000, INFRACFG1020e000, HOSTCSR18060000. ncc_power_window receives(host,conn,id) correctly, stores id/host before check. Fresh_owned is set only after original low5=0x12, both offacks, rails/PMIC/pins coldchecks, and beforepowerup; saved/domain_touched armed; IDgate passed. No ordinary code between the outerIDread and inner firstcheck intentionally clears those ownership flags. A hardware/raw race, dynamic acknowledgement change or ownership-state anomaly remains possible, but cannot be selected from this log.

## What next observability can discriminate (no new accesses requested)

The new snapshot logs FIRST failure once, reads only untilfailure and prints afterward. Decode only validbits:
- owner_ok bitmap covers0fresh,1notdirty,2saved,3domain_touched,4IDmatch,5SPMmap,6INFRAmapping,7GALSmapping,8HOSTmapping.
- owner_fail is ONE-based failingownerfield,0 means none. APfailed7 is sentinel for ownerfailure, not HOSTfailure.
- APfailed0..6 is zerobasedtable above; raw[i] is meaningful ONLY when valid&(1<<i). Unread rawzero is not measuredzero. Validmask1 means pwr was read; validmask3 means pwr+ack0, etc.
- If no ownerfailure and APfailed4/5, compare masks only; do not interpret unrelated sharedprotection bits. If failed6, capture bit5 rather than broadlydeasserting hostprotection/wakecontrol. No guessedwrites.

Evidence needed before any fix is the frozen-image firstfailure owner/valid/raw lines. Existing outer restored1 remains proof of outercleanup, not commonexecution or allfutureguardvalues. No native/active/device/CI edits made by this audit.

Primary exact sources:
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c#L132
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/clk/mediatek/clk-mt6885-pg.c#L1320
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_consys_reg.c#L533

One additional completeness distinction: active consys_check_reg_readable:533–615 checks bit13 and bit2 protections, GALSbit0, then resets/polls host clock-detect and checks host timeoutIRQbit0 before bit5. Our seven predicates do not reproduce that detector-write sequence; outer has already read realchipID. That is a deliberate narrower read-only guard, not proof a protection bit has opposite polarity. No detector/reset write is proposed here.
