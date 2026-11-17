---
name: nord2-jbts1-sileo-ssh-trollstore
description: "jbts1 payload (Sileo + OpenSSH + TrollStore, rootless /var/jb) built and VM-rehearsed 2026-11-15, not yet on the phone; no kernel change needed"
metadata:
  node_type: memory
  type: project
---

gpu/build/jbts-20261115: build_jbts.py (verified Procursus debs + TrollStore 2.1.1), txn_jbts.sh KERNEL_IMG TAG (VARIANT jbts1-off/-on/-sshoff/-sshon, UNDO=1), SETUP.md user guide, NOTES.md section "jbts1".
**Why:** user wants easy IPA installs on their own phone; our AMFI stand-in already accepts any validated signature + entitlements, so no kernel piece.
**How to apply:** SSH is Wi-Fi only (no USB net on Nord2); password in jbts-20261115/secrets/ssh-password.txt unless SSH_PASSWORD set; first-boot log /var/jb/var/log/nord2-jb.log.
