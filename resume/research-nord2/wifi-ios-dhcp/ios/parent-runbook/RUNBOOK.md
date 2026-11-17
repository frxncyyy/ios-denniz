# Parent-only DHCP pipeline runbook

Prepared commands only; none were executed by this reviewer. Current local branch is `codex/nord2-bringup`. **Use remote `fork` / GitHub `<github-user>/ios-sweet`**, as successful CI39859151068 did. `origin` points to dipa-studio and is not that successful CI repository. Workflow push trigger is main-only, so this branch needs one explicit dispatch. The workflow cancels older runs on the same ref; do not dispatch a duplicate.

The earlier migration concern is superseded by parent's actual nine-station-group PASS. Preserve current passing fixtures; do not perform a speculative migration.

## 1. Final reviewed source, generator and one CI

After parent finishes source integration and the already selected tests/compiles:

```sh
git diff --check
sh tools/nord2-mkpatch.sh
git diff --stat
```

Review/stage the exact intended source, generated0082, test and handoff changes; use the parent's final commit title. Do not `git add -A` research artifacts.

```sh
git diff --cached --stat
git commit -m 'nord2: align station BMC field and refine DHCP diagnostics'
dhcp_source=$(git rev-parse HEAD)
git push fork HEAD:codex/nord2-bringup
gh workflow run xnu.yml --repo <github-user>/ios-sweet --ref codex/nord2-bringup \
  -f macos_version=13.5 -f kernel_config=DEVELOPMENT -f machine_config=SWEET -f keep_going=true
gh run list --repo <github-user>/ios-sweet --workflow xnu.yml --branch codex/nord2-bringup \
  --event workflow_dispatch --commit "$dhcp_source" --limit 5 \
  --json databaseId,headSha,status,conclusion,url
```

Choose the single newly dispatched run with that exact full headSha; set `dhcp_run` to that actual databaseId, never a historical run. Keep the existing macos-14/Xcode15/iig workflow unchanged. Defaults above reproduce the successful13.5/DEVELOPMENT/SWEET generic ARMv8.2 pipeline.

## 2. One watcher/download, exact source and ISA

Prepare fresh tools without running them:

```sh
python3 research/nord2/wifi-ios-dhcp/ios/parent-runbook/prepare-ci-tools.py \
  --run "$dhcp_run" --source "$dhcp_source"
```

Then parent launches this **sole** watcher (45-second waits, change-only status), which checks exact source/attempt1, saves run.json/build.log and downloads only after success:

```sh
python3 "research/ci-$dhcp_run/watch.py"
python3 "research/ci-$dhcp_run/audit-artifact.py"
python3 tools/nord2-check-rcpc2.py \
  "research/ci-$dhcp_run/xnu-kernel/kernel.development.sweet" \
  > "research/ci-$dhcp_run/isa-audit.log"
```

The audit checks kernel SHA256SUMS, every generated0082 source plus AKS against the current regenerated source tree, and actual generic ARMv8.2 compile commands. Keep canonical/generated sources frozen through this comparison. Capture its exact kernel SHA/length and source/generic/ISA reports. If a rerun changes attempt, preserve the failed attempt and review/adapt the fresh watcher explicitly; its attempt1 guard must not be bypassed silently.

A fresh bounded linked review is still required for changed STA serialization/caller and RX diagnostics. Do not reuse Q's successful linked verdict/manifest as D approval or run Q/extract.py against new artifacts: it embeds old source/compile pins and output paths. Parent/assigned reviewer prepares D's bounded extractor/review with actual new artifact identity.

## 3. Required pending metadata before any image work

D=`research/nord2/wifi-ios-dhcp/ios` remains CLOSED now. Parent must update deliberately:

- `identity-required.json`: actual full source, CI/attempt/run.json path, exact kernel path/SHA/length; verify then mark parent_verified.
- `source-proposals.json` and `native-schema-pending.json`: final reviewed source/fixture/script pins, actual candidate/review manifests, new D-specific paths, fresh linked review. Refresh all modified stage/verifier/reader hashes. Do not inherit Q's b441429/CI39859151068/08e6 approval pins.
- `native-schema-required.json`: promote the complete reviewed schema only after exact source equality, generic flags, ISA and fresh linked proof. Preserve normal HW0+token, GTK, scan-return and diagnostic limits; mark DHCP-specific proposal review only for the actual selected final proposal. A narrow STA255 intent is source parity, never a proven DHCP fix/lease claim. Additional RX diagnostic intent must match actual final emitters/readers.
- Keep six readers unchanged unless a separately reviewed change exists. Local core-receipt/lifecycle copies are complete and byte-identical. Legacy full startup reader rejects some new post-Join RX families; preserve its filters and use the separate immutable pre-Join startup receipt plus other five post-Join readers. No stale full-log parser verdict may open source/image gates.
- Keep `prepared-image-required.json` false/null until actual independent image verification. Keep human/RF/conditional-recovery fields false and all image/serial/consent identities null until new exact human approval. Recovery identities d9a/6e4/d78 remain unchanged and unapproved for this future test.

Check preflight only after parent has supplied the verified identity/schema:

```sh
python3 research/nord2/wifi-ios-dhcp/ios/verify-image.py --preflight
```

## 4. One fresh image build, then independent actual-image review

Ensure target image and D/image-build.log do not already exist; the builder refuses replacement. Parent alone then runs:

```sh
(
  ulimit -v 1048576
  sh research/nord2/wifi-ios-dhcp/ios/build-image.sh
)
```

The builder checks ISA, strips only a temporary kernel, builds the unchanged calibrated raw6 identity ADT, payload/loader, packs the known rescue container and validates the actual image. It emits D/manifest.json, D/payload.bin and D/image-build.log. Confirm `FAILURES: none` and exact image/payload/kernel hashes, lengths and boot spare.

Send an explicit IMAGE_READY with those actual pins to the independent reviewer. Do not open prepared gate before a fresh existing-image PASS. Do not invoke run-approved-boot.py here. The next runner requires a newly verified prepared gate and fresh exact-image human/RF/conditional-recovery authorization;08e6 approval and used marker are not transferable. No stock Android boot over iOS userdata.
