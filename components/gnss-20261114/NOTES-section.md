## 2026-11-14 ~02:10 GNSS (punto 7): GNSS line ported to the q10jcp chain (0580/0581) + image q10jcs + payload gnssfix1 -- host/build/VM rehearsal only, NOT on the phone
Dir gpu/build/gnss-20261114 (make_patch.sh, a/ = q10jcx stage (sweet_touch.c identical to q10jcp), b/ = 0580, c/ = 0581, test/run.sh, pl/ = payload generator + rehearsals).
**Survey (V = verified in files/logs):**
- [V] The real GNSS work exists (12 Nov, GNSS_ENGINE.md, hwcodec/gnss): MT6893 GNSS = CONNAC2 conn_infra sub-block; no firmware file: the DSP
  code is uploaded by libmnl over the data link. Kernel line 0350..0364 (power ladder, USRT/BGF-DMA rings at 0xDFE00000, S11 = in-kernel
  DSP code download) proven on the phone: gl11a/gnssd3 logs `S11 RESULT: DSP booted (50/50 fragments acked)`, DSP frames 0xfe2c/0xfe30/
  0xfe37/0xfe4e/0xfe61 flowing. libmnl.so runs NATIVELY on iOS inside n2mnld (TPIDR canary patch, aligned pthread table): gnssd3
  `mtk_gps_mnl_run -> 18 (engine threads running)`.
- [V] That kernel line was NEVER in the integration line (run_integ.sh uses build_q10jfi.sh with GNSS=0): q10jcp has no /dev/gpsdl0.
  The phone's current "city" position is Wi-Fi/Apple location, not GNSS. Mirror (01:15) has no gnss/locbridge job; locfix2 installed.
- [V] Unanalysed blocker: in BOTH gnssd3 runs (q10jfg 21:52 and q10jfj 22:26, 12 Nov) the USB console died mid-line 1..3 s after the
  engine threads started (libusb -7), next boot came up in fastboot; the same K=31 HOLD with the engine process dead (gnssd1b/gnssd2)
  stayed up 6 min. The engine log was deleted by the gnssd3ju undo (not recoverable from the mirror). Cause unknown; most likely (I) an
  AXI access to a sleeping GPS domain from the pipe (hard hang, no panic line), or something the first libmnl runtime frames trigger.
**Kernel (host-tested + built, NOT on the phone):**
- 0580 nord2-gnss-port (ae28c6ae): nord2_gpsdl.inc + nord2_gnss_pwr.inc = the 12 Nov final files; nord2_gnss_bind.inc = the 0351..0363
  sweet_touch.c binding moved to a file; sweet_touch.c gets 1 include + 1 start call. io_submap: the 3 AO pages (SPM/GALS/infracfg) reuse
  the Wi-Fi probe's nw.spm/nw.gals/nw.infra (were 3 NEW ml_io_map = all 3 free pages), the 0355 io_submap controls are removed; every
  other mapping is ml_io_map_unmappable. Nodes on by default (n2gps=1 no longer fits the command line; n2gdv bits all taken): kill switch = sysctl kern.n2gnss bit 0 (ladder + pipe ENODEV) and the payload gate/undo.
  Creating the nodes touches no register; hardware is reached only by ladder ioctls (Wi-Fi guard first) and the HOLD pipe.
- 0581 nord2-gnss-pipe-guard (048cade5): every read/write first checks the Wi-Fi guard (quiet) and gps_top_off == ACTIVE through the
  conn_infra host debug mux (0x180600b0 = 0x80 / 0x18060240 [3:0] == 2; host CSR page only): if not active no GPS-bus register is
  touched (kern.n2gnss bit 1 = observe only). Refusals are transient (EAGAIN; pipe down only after 3000 in a row). Quiet TX (0580 printed 5
  lines per write). Console: `NORD2 GNSS PIPE: about to TX write #n ... id` (first 24, printed BEFORE the DMA), `MCUB d2a flag`,
  `gps_top_off .. -> ACTIVE/NOT ACTIVE`, heartbeat every 2 s `NORD2 GNSS PIPE: t=..s rd= .. rx= .. wr= .. top= ..`.
- Host: test/run.sh -> `GNSS PIPE TEST OK` (c/ and b/ files compiled with kernel shims + fake conn_infra/GPS bus: no GPS-bus access when
  top inactive or guard refuses, data path, quiet TX, pipe-down, heartbeat, kern.n2gnss bits) + `GNSS M1+M2 TESTS OK`.
- **Image q10jcs** = q10jcx recipe + 0580 0581 (gnss-20261114/build_q10jcs.sh; MORE_PATCHES ... 0551 0580 0581 0640), built under the heavy lock after the OOM crash (the pre-crash q10jcs dir was deleted, not reused):
  device-mmu-q10jcs-candidate/ui-sched/nord2-ios-metal-q10jcs-ui654fb60dvfs-cap3500-ram5088-sched.img, 15175680 B,
  sha256 bf3df0450ab5d64e337cb52aa8d29e022f22e1bffb715fd6f2ccddb0aa84e82a. FAILURES: none, BUILD_EXIT 1 (known 0399 IMAGECHECK),
  WIFI_MEMBERS_OK, SCANPATH_OK (GNSS functions in SCANPATH_EXTRA_ALLOWED: gnss-20261114/scanpath_extra_gnss.txt). Command line identical
  to q10jcx (603, 4 free), boot_spare 18378752. Kernel strings checked: gpsdl nodes, S11, PIPE, kern.n2gnss.
**Userland (built + VM-rehearsed, NOT on the phone):** n2mnld gnssd4 (hwcodec/gnss/n2mnld/native, sha 0349948d...: gnssd3 + console
  milestones `first libmnl frame written`, `first DSP bytes`, engine log fsync per stats line, first stats at 3 s; import trace off).
  Payload gnssfix1 (gnss-20261114/pl: guest-gnssfix1.sh / -undo / roundtrip / guest-gnsslog.sh read-only log dump, owned-gnssfix1*.json):
  n2mnld job com.nord2.gnssd (60 s delay, kboot K=31, one run per boot) + n2locbridge (locbridge1 binary) in FIFO mode
  (com.nord2.locbridge: valid fixes -> locationd via CLSimulationManager). Rehearsal vs the 01:51 mirror (phone with fin1+sbshift1+api1): ROUNDTRIP_OK (install, no-op reinstall, undo == start, no-op undo);
  txn path generic_update_offline.py GNSSFIX1_OK + check_update_catalog_fast.py exit 0 (11 changed, 0 outside owned). txn: pl/txn_gnss.sh.
**Status:** step 1 (DSP boot) proven on old images, expected to repeat on q10jcs; step 2 (NMEA from libmnl) and step 3 (locationd) never
  observed: the next phone run is the first one. A real position needs sky view (user, outdoors).
