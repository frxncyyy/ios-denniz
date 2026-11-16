# jbts inputs (not in this repository)

`build_jbts.py` expects these files in `inputs/` and verifies them on every run (Procursus InRelease signature with the
Procursus key from GitHub `memo.gpg`, Packages.xz sha256, every .deb against Packages, TrollStore 2.1.1 GitHub asset
digests, pinned ldid sha256). Download them yourself from apt.procurs.us (suite 1800) and the opa334/TrollStore and
opa334/ldid GitHub releases. File names used by the author (2026-11-15):

## inputs/procursus/
- `InRelease`
- `Packages.xz`
- `added-packages.json`
- `bootstrap-packages.json`
- `bootstrap-verify.json`
- `memo-github.gpg`

## inputs/trollstore/
- `TrollStore.tar`
- `com.opa334.trollstorehelper_2.1.1_iphoneos-arm64.deb`
- `ldid`

## inputs/debs/
- `apt_2.9.4-1_iphoneos-arm64.deb`
- `base_2_iphoneos-arm64.deb`
- `bash_5.2.21_iphoneos-arm64.deb`
- `ca-certificates_20240602_all.deb`
- `chariz-keyring_2021.07.18_all.deb`
- `coreutils_9.5_iphoneos-arm64.deb`
- `darwintools_1.6_iphoneos-arm64.deb`
- `dash_0.5.12_iphoneos-arm64.deb`
- `debianutils_5.17-1_iphoneos-arm64.deb`
- `diffutils_3.10_iphoneos-arm64.deb`
- `diskdev-cmds_718_iphoneos-arm64.deb`
- `dpkg_1.22.6_iphoneos-arm64.deb`
- `essential_0-5_iphoneos-arm64.deb`
- `file-cmds_430.100.5_iphoneos-arm64.deb`
- `findutils_4.9.0-1_iphoneos-arm64.deb`
- `gpgv_2.4.5_iphoneos-arm64.deb`
- `grep_3.11_iphoneos-arm64.deb`
- `havoc-keyring_2022.06.03_all.deb`
- `launchctl_1:1.1.1_iphoneos-arm64.deb`
- `libapt-pkg6.0_2.9.4-1_iphoneos-arm64.deb`
- `libassuan0_2.5.7_iphoneos-arm64.deb`
- `libbrotli1_1.1.0_iphoneos-arm64.deb`
- `libcrypt2_4.4.36_iphoneos-arm64.deb`
- `libdimentio0_1:0~20230202.7ffffff_iphoneos-arm64.deb`
- `libedit0_3.1-20230828_iphoneos-arm64.deb`
- `libffi8_3.4.6_iphoneos-arm64.deb`
- `libgcrypt20_1.10.3_iphoneos-arm64.deb`
- `libgmp10_6.3.0_iphoneos-arm64.deb`
- `libgnutls30_3.8.5_iphoneos-arm64.deb`
- `libgpg-error0_1.48_iphoneos-arm64.deb`
- `libhogweed6_3.9.1_iphoneos-arm64.deb`
- `libidn2-0_2.3.7_iphoneos-arm64.deb`
- `libintl8_0.22_iphoneos-arm64.deb`
- `libiosexec1_1.3.1_iphoneos-arm64.deb`
- `libkrw0_1.1.1-2_iphoneos-arm64.deb`
- `liblz4-1_1.9.4_iphoneos-arm64.deb`
- `liblzma5_5.4.4_iphoneos-arm64.deb`
- `libmd0_1.1.0_iphoneos-arm64.deb`
- `libncursesw6_6.4_iphoneos-arm64.deb`
- `libnettle8_3.9.1_iphoneos-arm64.deb`
- `libnpth0_1.7_iphoneos-arm64.deb`
- `libp11-kit0_0.25.3_iphoneos-arm64.deb`
- `libpam-modules_1000.0_iphoneos-arm64.deb`
- `libpam2_20230627_iphoneos-arm64.deb`
- `libpcre1_8.45-1_iphoneos-arm64.deb`
- `libpcre2-8-0_10.43_iphoneos-arm64.deb`
- `libreadline8_8.2.0-1_iphoneos-arm64.deb`
- `libssl3_3.2.1_iphoneos-arm64.deb`
- `libtasn1-6_4.19.0_iphoneos-arm64.deb`
- `libunistring5_1.2_iphoneos-arm64.deb`
- `libxxhash0_0.8.2_iphoneos-arm64.deb`
- `libz-ng2_2.1.6_iphoneos-arm64.deb`
- `libzstd1_1.5.5_iphoneos-arm64.deb`
- `ncurses-bin_6.4_iphoneos-arm64.deb`
- `ncurses-term_6.4_all.deb`
- `openssh-client_9.7p1-1_iphoneos-arm64.deb`
- `openssh-server_9.7p1-1_iphoneos-arm64.deb`
- `openssh-sftp-server_9.7p1-1_iphoneos-arm64.deb`
- `org.coolstar.sileo_2.5.1_iphoneos-arm64.deb`
- `procursus-keyring_2020.05.09-4_all.deb`
- `profile.d_0-7_iphoneos-arm64.deb`
- `sed_4.9_iphoneos-arm64.deb`
- `shell-cmds_309_iphoneos-arm64.deb`
- `shshd_1.1.1.1_iphoneos-arm64.deb`
- `snaputil_12.3_iphoneos-arm64.deb`
- `sudo_1.9.15p5_iphoneos-arm64.deb`
- `system-cmds_979.100.8_iphoneos-arm64.deb`
- `tar_1.35_iphoneos-arm64.deb`
- `uikittools_2.1.7_iphoneos-arm64.deb`
- `vi_070224_iphoneos-arm64.deb`
- `zsh_5.9_iphoneos-arm64.deb`
