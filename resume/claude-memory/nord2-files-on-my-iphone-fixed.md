---
name: nord2-files-on-my-iphone-fixed
description: "On My iPhone / TrollStore picker fixed 2026-11-16 with q10jfinal44ts (0732 lsd memfloor, 0734 root fstypename apfs)"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-16T19:02:25.463Z
---

2026-11-16: user confirmed "Su questo iPhone" now shows in Files and the TrollStore IPA picker on **q10jfinal44ts** (sha f55a15ea, kernel only, flashed with autocycle/flash_boot.sh). TrollStore opens again after q10jfinal42ts + ipainbox fix2.

Root causes (verified from readable logs unless noted):
- lsd killed by jetsam per-process limit (16/12 MB) -> TrollStore unregistered. Fix 0732: kernel raises limits to 128 MB for lsd/installd/pkd/fileproviderd/containermanagerd; fix2 payload re-registers apps (trollstorehelper refresh + uicache).
- fileproviderd log: "Non-eligible device /dev/ufsroot0: non-apfs" because statfs reported hfs. Fix 0734: report root type name as apfs when fileproviderd execs (kern.n2fsapfs=0 undoes). 0733 (answer APFS 'J' ioctls for fileprovider processes) never fired; not the cause.
- Found by reading public repo DenialOfGravity/ios-redfin (commit 101171d was a related but different symptom). fplog1 payload (readable FileProvider/DocumentManager/pluginkit logging + File Provider Storage folder) made the diagnosis possible.

**Why:** the user wants IPA install via TrollStore and a working Files app.
**How to apply:** keep q10jfinal44ts as the line; watch for regressions from system-wide apfs type name (apps probing APFS clone/snapshot); undo with sysctl kern.n2fsapfs=0 or reflash 43ts (8f91f1a6). See [[nord2-jbts1-sileo-ssh-trollstore]].
