PREPARED ONLY. No image was built and no hardware/device command was created.

CI39506694221, exact source9327714bfe56719881e4c6251243fcd6afc97b24.
Each verifier is deliberately blocked by PENDING_PARENT_VERIFIED_KERNEL_SHA256.
Parent must provide the downloaded successful artifact's exact hash, then pin it
in each verifier/template before an offline build can pass preflight. Existing
images/buildlogs/payloads/manifests are never implicitly overwritten.

- diagnostic/nord2-ufs-linereset-stop23.img: same exact379-byte RO commandline
  as the prior2ec05f preflight image;13exact startup-gate requirements retained.
  Intended stop before root, no Linux return, no UFS publication/writes. New
  engine admits only exact valid LINERESET80000010 in addition toUECPA0.
  parse-report.sh invokes the20-line parser explicitly with --allow-line-reset;
  historical8702715 captures must keep the default original policy.
- ios/nord2-ios-wifi-common-stop23.img: priorce84 RW observer commandline plus
  exactly nord2wificommon=1. No lane diagnostic, zoom token, EMI query or CPU
  clock telemetry. Input/radio2/firmware verifier/strategy costs/FBdiag retained.
  Common WiFi initialization is requested; no successful result/RF claim made.

Actual output paths are shim/out/nord2-xnu with the names above. Source scripts
and private evidence reside here under diagnostic/ and ios/. Build scripts are
offline only; none were run. No flash/boot/runners or USB operations included.

Independent verifier inherits the previous actualpayload-segment checks:
strip pinned MachO, apply expected chained fixups, compare every payload segment
address/vmsize/filesize/bytes and entry/count, in addition to full608-byte boot
args, originalLinux/BSS/bothDTs, tested rescue rawramdisk, Androidv2 headerID,
partition size, exact successful CI source and kernelSHA256SUMS+pinnedhash.
Expected transformation reuses mkpayload parser/fixup code; it is not a second
independent chained-fixup implementation. Final manifests are produced only
when actual built artifacts pass verification; template files claim no runtime
outcome, speed gain, measured CPU frequency or UFS lane success.

Test: python3 research/nord2/ufs-linereset-common/test-templates.py
Checks exact old commandline identities/difference,379B diagnostic, CI/source,
strict pending-hash refusal before filesystem prerequisites, payload-segment
verification retained, shellsyntax, and absence of new buildoutputs/manifests.
