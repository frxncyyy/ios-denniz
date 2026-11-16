# jbts1 — Sileo + OpenSSH + TrollStore on the Nord2 iOS 16.7.16 port

Your own phone, your own kernel. No FairPlay/App Store DRM, Anisette or attestation work is involved: TrollStore installs IPAs you
already have (unencrypted / decrypted ones; App Store-encrypted binaries cannot run on this port, cryptid 1 is refused by the kernel).

## What gets installed

| piece | where | from |
| --- | --- | --- |
| Procursus rootless prefix (dpkg 1.22.6, apt 2.9.4, zsh, bash, coreutils, sudo, uikittools...) | `/var/jb` (= `/private/var/jb`) | Procursus 1800 debs, InRelease signature checked against the Procursus key from GitHub |
| Sileo 2.5.1 | `/var/jb/Applications/Sileo.app` | Procursus `org.coolstar.sileo` |
| OpenSSH 9.7p1 (server, client, sftp) | `/var/jb/usr/sbin/sshd`, config `/var/jb/etc/ssh/sshd_config` | Procursus |
| TrollStore Helper 2.1.1 (app + `trollstorehelper`) | `/var/jb/Applications/TrollStorePersistenceHelper.app` | opa334/TrollStore release deb (GitHub digest) |
| TrollStore 2.1.1 + ldid | installed at the first boot from `/var/jb/usr/share/nord2-jb/TrollStore.tar` into `/var/containers/Bundle/Application/<uuid>/TrollStore.app` | opa334/TrollStore + opa334/ldid releases |
| boot jobs | `com.nord2.jb.init` (`/var/jb/usr/libexec/nord2-jb/jbinit.sh`), `com.nord2.jb.sshd` (`sshd-run.sh`) in `/System/Library/xpc/launchd.plist` | this payload |
| helper | `/var/jb/usr/bin/nord2-jb` | this payload |

Layout: **rootless** (`/var/jb`), the layout every iOS 15/16 tweak repository targets (packages of architecture `iphoneos-arm64`).
Everything is plain arm64 (A11 identity, no arm64e/PAC needed). Code signing: our kernel's AMFI stand-in (`sweet_amfi.c`)
accepts every signature XNU validates, gives every binary on the root volume (that includes `/private/var`) platform status and
grants the XML entitlements in the signature; the sandbox policy is permissive. TrollStore's fake-signed binaries
(ldid + CoreTrust-bypass layout: SHA-1 template CD + real SHA-256 CD) pass because XNU uses the SHA-256 CD; CoreTrust is not
involved at all. **No kernel change is needed**; any current kernel image works.

## Build the payload (PC)

```sh
cd gpu/build/jbts-20261115
# optional settings (defaults: random 16-char password for root AND mobile, password login on, root login on)
export SSH_PASSWORD='choose-one'            # or SSH_PASSWORD_FILE=/path/to/file
export SSH_PUBKEY=$HOME/.ssh/id_ed25519.pub   # recommended: key login for root and mobile
# export SSH_PASSWORD_AUTH=no                 # keys only (needs SSH_PUBKEY)
# export SSH_ROOT_LOGIN=prohibit-password     # root by key only
python3 -I build_jbts.py                      # -> payload-jbts1*/, owned-jbts1*.json, build.json
cat secrets/ssh-password.txt                  # the generated password, if you did not set one
```
`secrets/` (mode 700) keeps the SSH host keys and the password hash; they are reused by later builds, so the phone's host key
fingerprint stays the same. Delete `secrets/` to make new ones.

## Install / switch / remove (PC, phone in fastboot, same transaction pattern as txn_gfxall.sh)

```sh
cd gpu/build
jbts-20261115/txn_jbts.sh <KERNEL_IMG> <TAG>                      # install
VARIANT=jbts1-sshoff jbts-20261115/txn_jbts.sh <KERNEL_IMG> <TAG>  # kill switch: SSH off  (jbts1-sshon = back on)
VARIANT=jbts1-off    jbts-20261115/txn_jbts.sh <KERNEL_IMG> <TAG>  # kill switch: all boot jobs off (jbts1-on = back on)
UNDO=1               jbts-20261115/txn_jbts.sh <KERNEL_IMG> <TAG>  # remove everything
```
The undo removes `/var/jb` (with whatever Sileo/apt added), both jobs, every app bundle installed by TrollStore (their data
containers stay), restores the SpringBoard preference and moves the LaunchServices caches aside (full rescan at the next boot).
Re-running the install with the same build is a no-op apart from re-applying the SSH settings (it resets the passwords to the
configured ones).

## On the phone

First boot after the install (about 20-60 s after the lock screen): `jbinit.sh` writes `/var/jb/etc/pwd.db`, runs the bootstrap
postinst scripts once, registers Sileo and TrollStore Helper with LaunchServices, then installs TrollStore (+ ldid) once.
Log: `/var/jb/var/log/nord2-jb.log`; sshd log `/var/jb/var/log/sshd.log`.

### SSH (Wi-Fi; this port has no USB networking, so no iproxy/usbmux)
1. Join Wi-Fi in Settings, read the phone's IP (Settings > Wi-Fi > (i)).
2. `ssh root@<ip>` or `ssh mobile@<ip>` (password from the build, or your key). First connection: compare the host key
   fingerprint with `ssh-keygen -lf gpu/build/jbts-20261115/secrets/ssh_host_ed25519_key.pub`.
3. `nord2-jb status` shows the switches, sshd, addresses, TrollStore and the log tail.
4. Change passwords: `passwd root`, `passwd mobile`. Keys only: `nord2-jb password-auth off`. SSH off/on: `nord2-jb ssh off|on`.
   File transfer: `scp file.ipa mobile@<ip>:/var/mobile/Documents/` (sftp subsystem is enabled).

### Sileo
Home screen icon "Sileo" (or `uiopen sileo://` over SSH). Source Procursus is preconfigured; add others (e.g. Havoc, Chariz) in
Sources. Tweaks that need injection will install but do nothing: there is no tweak injector on this port.

### TrollStore: install an IPA
- Home screen "TrollStore" (if it is missing: open "TrollStore Helper" > Install TrollStore, or over SSH `nord2-jb trollstore`).
- Copy the IPA to the phone (Safari download into Files, or scp), then share it to TrollStore / open it with TrollStore, or
  TrollStore > "+" > Install IPA File. Over SSH: `/var/jb/Applications/TrollStorePersistenceHelper.app/trollstorehelper install /path/app.ipa`.
- Settings > Install Method: if "installd" hangs, switch to "Custom" (installd's placeholder install is untested on this port).
- After anything rebuilds the LaunchServices database (some of our other payloads move its caches aside), TrollStore apps lose
  their icons: TrollStore > Settings > "Refresh App Registrations", or `nord2-jb refresh`.
- "Developer Mode required" after an install: harmless here (the kernel does not enforce developer mode); dismiss it.

## Offline IPA install: ipa-inbox (no network, no UI)

```sh
cd gpu/build
# ready-made: payload-ipainbox-games2 = Terraria 4.5.0 + Subway Surfers 3.71.10 (the versions now on the phone) + --apps-undo
jbts-20261115/txn_ipainbox.sh games2 <KERNEL_IMG> <TAG>
# other IPAs: python3 -I jbts-20261115/make_ipainbox.py NAME [--apps-undo] A.ipa B.ipa   (hard links, no copy)
```
- `--apps-undo` first removes `/Applications/Terraria.app` and `/Applications/SubwaySurf.app` (put there by the old apps payload and
  payload-ssclean1; only if bundle id and main-executable sha256 equal one of those IPAs). Do not run ssclean1's undo afterwards (it
  would put its old modded Subway back into /Applications). Without that TrollStore answers 179 (same id as a system app).
  Their data containers (saves) stay and are reused by the TrollStore install (same bundle id).
- The job `com.nord2.jb.ipainbox` (RunAtLoad + WatchPaths) installs every `*.ipa` in `/var/mobile/Documents/ipa-inbox` with
  `trollstorehelper install` about 2 min after boot: `done/` or `failed/<name>.rcN`, log `ipa-inbox/result.log`.
  Over Wi-Fi the same inbox works: `scp app.ipa mobile@<ip>:/var/mobile/Documents/ipa-inbox/`.
- `ipa-inbox/force` = install with `force` (replaces an app not installed by TrollStore, rc 171; never bypasses 179).
- Once per boot the job also runs `trollstorehelper refresh` (TrollStore apps lose their registration whenever LaunchServices
  rebuilds, e.g. after --apps-undo).
- Kill switch: `/var/jb/etc/nord2-jb/ipainbox-disabled`. jbts1's undo also removes this job (the inbox folder stays).

## "Install IPA File" in TrollStore: kernel piece 0730
The document picker is a PlugInKit extension (`com.apple.DocumentManager.Service`). Every extension asks the Sandbox for its own
container path at start; our sandbox stand-in answered ENOENT, PlugInKit threw "plugin has invalid sandbox or container" and the
extension aborted -> TrollStore shows iOS' generic "Couldn't communicate with a helper application". Piece 0730 answers that
question for `.appex` processes with their own HOME (data container), image q10jfinal40ts (= q10jfinal40bt + 0730).
Switch: `sysctl kern.n2sbcont=0` (old behaviour), `=2` also for containerized apps; boot-arg `n2sbcont=N`.

## Credits and licences
- Procursus (ProcursusTeam, various licences per package; https://github.com/ProcursusTeam/Procursus), Sileo (Sileo Team / Amy While,
  BSD-4-Clause), OpenSSH (BSD), unpacked unmodified from their .debs (master.passwd gets an `sshd` alias line and our password hashes;
  sshd_config is ours).
- TrollStore and ldid fork by opa334 (MIT; https://github.com/opa334/TrollStore, https://github.com/opa334/ldid), shipped unmodified.
- HakiOS by oosexgang (licences per directory, see its NOTICE; https://github.com/oosexgang/hakios-xnu-kernel `tools/mk-jb.sh`, `userland/rc.jobs.{sileo,ssh,users}`):
  the per-boot prep (firmware + pwd_mkdb + postinsts + uicache registration retry), dropping libkrw0/libdimentio0/shshd, removing the
  'firmware' dependency from installed records, the apt CaInfo line, keeping set-ID bits, PAM off for the prefix's sshd. Reimplemented
  here; none of their scripts were run or copied verbatim.
