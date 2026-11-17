# EMI permission interpretation from available evidence

Read-only review, 2026-10-21. No new disassembly, device operation, SMC, MPU change, firmware placement, or active source change. This reuses REPORT.md/independent-review.json and their existing focused ATF excerpt, plus selected Android source04d00c25 and saved DT/log/iomem.

A complete error-free128-return snapshot establishes32 sets of SA, EA, APC0 and APC1 register observations through the reviewed legacy ABI. It does **not** by itself establish effective AP/CONN permission, region enable state, lock state, or exclusive ownership. It is also a sequential snapshot, not an atomic hardware snapshot. Current executing monitor identity remains unresolved as already documented.

## Addresses and domain-field interpretation

Selected `drivers/misc/mediatek/emi/mt6885/mt_emi.h:34–40` gives SA=100+4r, EA=200+4r, APC=300+4r+100g (hex), r0..31,g0..1. All128 offsets are aligned and within reviewed legacy read bounds. DT `dtb/nord2/live.dts:4586–4602` gives align16, DRAM40000000..23fffffff,32regions/16domains.

The selected display decoder `drivers/memory/mediatek/emimpu_old.c:80–110` uses:

- start=(uint64(SA)<<16)+40000000;
- inclusive end=(uint64(EA)<<16)+40000000+ffff;
- domain d field=(APC[d/8]>>(3*(d%8)))&7.

Check full raw return validity before decoding, uint64 arithmetic, start<=end and DRAM bounds. Keep raw high/reserved bits; do not silently mask an invalid observation into a plausible policy. End is inclusive in64KiB units. Thus raw SA=EA=0 describes a candidate64KiB interval40000000..4000ffff under the source formula, **not proven disabled**.

`include/memory/mediatek/emi.h:18–24` defines0 unrestricted,1 secureRW,2 secureRW/nonsecureR,3 secureRW/nonsecureW,4 secureR/nonsecureR,5 forbidden,6 secureR/nonsecureRW. Code7 is not defined there (sysfs prints NONE), so classify it unknown/reserved rather than unrestricted or disabled. These are per-domain/security permission labels, not proof of the security attributes of a particular transaction.

`conninfra/platform/mt6893/mt6893_emi.c:37–41,88–102` explicitly names APdomain0,CONNdomain2,SCPdomain3 and programs region27 over the CONN allocation with0 for these domains. Region initialization sets other domains5. This establishes selected Linux's intended domain assignment/policy. It does not independently prove current hardware master-to-domain routing or current inherited policy. A domain2 field alone does not identify every CONN access path/security attribute. Source remapping in mt6893_pos.c:102–126 must also be distinguished from physical MPU addresses.

## Overlap priority: unresolved, not guessed

No overlap arbitration rule was found in the inspected selected MPU implementation/header/DT or the already-reviewed ATF read/set helpers. Set order does not establish hardware priority. The Linux policy is suggestive: DT APregion31 spans DRAM and forbids domain2, while CONNregion27 grants domain2 on its carveout. This is consistent with an overriding narrower/lower-numbered region, but does not prove whether hardware chooses lower index, higher index, combines policies, or uses additional controls.

Until the selected hardware rule is established, list every interval intersecting each proposed firmware byte range and its AP/CONN fields. Never collapse overlapping conflicting policies to an effective allow based only on region27 or a guessed ordering. All32 regions are necessary for this overlap inventory, but not sufficient for priority proof. If all applicable possible interpretations permit the needed access, that is a conservative conditional result, still subject to enabled-state/master-routing and other protection layers.

## Enabled/disabled representation: unresolved

There is no proven enable bitmap among the128 fields. Linux's `mtk_emimpu_clear_protection` (`emimpu_old.c:864–890`) issues structured action1; it does not define hardware reset/disabled SA/EA/APC encodings. Backed-up ATF action1 is not a separately verified clear implementation: REPORT.md establishes other nonzero actions outside0/4/5 return their action value. Do not derive disabled semantics from this incompatible source caller or from all-zero/all-forbidden fields. Inverted ranges are invalid/unknown pending a hardware disable rule, not automatically ignored. Unknown regions must remain in the ambiguity set.

## Lock semantics: software lock is outside this snapshot

Linux packs requested lock into group0bit31 (`emimpu_old.c:850–853`) and its debug parser interprets bit31 as lock. The actual backed-up ATF setter excerpt is materially more specific: file offsets d254–d25c check a per-region software byte and refuse if1; d260–d26c set that byte when group0/requestbit31 is set. Then d298 masks the APC payload to00ffffff before hardware writes d2dc/d304. The lock request is therefore consumed by a monitor software table; raw APC bit31 must not be assumed to represent that table. Bit31zero does not prove unlocked; bit31one from a read is not independently established as effective lock. The128 read allowlist does not observe monitor software state. Lock does not itself grant/deny data access and no lock/unlock experiment is justified.

## Actual CONN reserve and ownership limits

Saved live DT `:1099–1105` marks dynamic `consys-reserve-memory` no-map,size450000,alignment100000,allocation range50000000..90000000. It does not fix the chosen base. Saved Android `backup/nord2/post-probe-dmesg.txt:1874` reports base8fb00000 and remap8fb0. Together they imply Android's observed allocation8fb00000..8ff4ffff. `backup/nord2/iomem.txt:416–418` ends one System RAM extent at8fafffff and resumes at8ff50000: the exact450000-byte omission corroborates the no-map carveout. It is not a named iomem resource and is distinct from the nearby8e000000..8f7fffff reserved range.

These saved Android facts do not prove current XNU allocator exclusion, lack of firmware/other-master use, current MPU permissions, or permission to overwrite the carveout. Read-validated SA4fb0/EA4ff4 would match this physical interval; that match is metadata corroboration only. Current XNU memory exclusion and actual remap/ownership require their own evidence before placement.

The observer report should therefore say **raw128 fields validated; effective AP/CONN permission unknown** unless each unresolved dependency is separately discharged. No SET, CLEAR, dump action, protection relaxation, test write, or firmware execution follows from a readable snapshot.
