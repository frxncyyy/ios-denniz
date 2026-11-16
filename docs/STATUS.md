# Port status: kernel lines, pieces, known limits

Tags: **verified** = observed on the author's phone (one unit); **host** = built/tested on the host only; **inferred** = from code,
disassembly or logs. Image hashes are given as sha256 prefixes of the author's per-device images (they are not reproducible from
this tree: they contain the author's pins).

## Kernel lines (most recent first)
| Line | sha256 prefix | Adds | Phone |
|---|---|---|---|
| q10jfinal44ts | f55a15ea | 0734: root reported as "apfs" to statfs once fileproviderd starts (kern.n2fsapfs) | verified: "On My iPhone" works |
| q10jfinal43ts | 8f91f1a6 | 0733: HFS answers APFS 'J' fsctls of the FileProvider stack (kern.n2hfsapfs) | ran (0733 never triggered) |
| q10jfinal42ts | - | 0732: memory-limit floor 128 MB for lsd/installd/pkd/fileproviderd/containermanagerd | verified: TrollStore + picker open |
| q10jfinal41ts | 1fce7efb | 0731: sandbox/AMFI log for the FileProvider stack | host |
| q10jfinal40ts | 5a1b2684 | 0730: sandbox op 4 container path for .appex processes | 0730 verified on 42ts+ (picker opens) |
| q10jfinal40bt | 866531b0 | BT bring-up starts at boot after en0 (bt22) | - |
| q10jfinal39bt | 7a8c5a3a | BT transport mode 2 default (AAC 230 kbit/s stable in bt39a) | verified |
| q10jfinal38bt .. 30bt | 1fc698b9, 011c5a70 (37bt), 4f13091e (36bt), 8d736dce (35bt), 0de54731 (34bt), ba190162 (33bt), ea42c6a9 (32bt), eafee24f (31bt), d9c6bf5b (30bt) | BT controller iterations bt10..bt20 | phone runs bt29a..bt38a |
| q10jfinal30 | 3301f088 | 0762 sensors r3 (light polling, summaries) | host |
| q10jfinal27 | 28ed2227 | 0761 device orientation | verified: auto-rotate |
| q10jfinal25 | 66eb02b1 | 0760 motion sensors to iOS | verified: accelerometer streams |
| q10jfinal23bt/24bt/26bt/28bt/29bt | b679dde6, 18cb3fa2, b54f3da2, 27e5706b, 853e5d66 | BT bt5..bt9 | phone runs |
| q10jfinal21 | - | 0750 GPU fault recovery | host |
| q10jfinal19 | 65bc8d2b | 0715 modem blob from a root-volume file | ran on the phone |
| q10jfinal18 / 18bt2 | d63ae64b / 7d444f2e | SIM 0710-0714 + BT 0720-0722 | phone runs bt18* |
| q10jfinal16sim2 | 7c4fbe0b | SIM/carrier 0710-0714 | host |
| q10jfinal14usb | d73e8f99 | USB/battery/charging 0700-0702 | host |
| q10jfinal13 | 2d7a82b5 | MDP chain up to 0688 + Wi-Fi auto-join 0790 | verified (2026-11-15 day) |
| q10jfinal8a | 69f0991e | GPU steps A/C, 0668, RAM8 heap 0650, MDP 0670-0681 | verified (2026-11-15 night) |
| q10jperf6f | 719fcd83 | GPU step A + C without 0664/0667 | verified |
| q10hapd6a | 5d2cf34f | haptic fixed gain 0x57 (0618) | verified (user) |
| q10jfin | - | GNSS 0583-0593 + haptic chain | verified: first GNSS fix 2026-11-14 |

Lines not listed were intermediate builds (q10j*, q10jmdp*, q10jram8*, q10jperf*) documented in the per-component READMEs/recipes.

## Known limits
- **5G**: not possible with the iPhone X (Mav17) baseband identity; CommCenter says "5G Switch support: kNotValid" [inferred from
  CommCenter disassembly + logs]. 0714 is an off-by-default experiment.
- **Compass app**: frozen; needs SPU-style device-motion fusion (0xFF00/0x10 service + 9-axis fusion in the kernel) [inferred].
- **Ambient light / Auto-Brightness**: hub never pushes light samples; 0762 polled fallback [host]; MobileGestalt key cached false.
- **GPU faults (0750)**: a translation fault used to leave the compositor's context permanently faulted (flashing display);
  0750 recovers it [host].
- **Memory pressure**: daemons killed under pressure; extra DRAM above the XNU window not usable as normal RAM (ram8c1/c1t/c1u/c1w
  do not boot); 0651 check regions border firmware memory of unknown extent.
- **L2 policy 0664/0667**: discarded (GPU data-invalid faults in Maps / slower).
- **Bluetooth**: no HFP/SCO, no HID input, controller always driver-owned (no low-power).
- **App Store**: genuine install NO-GO (FairPlay); use TrollStore/IPA.
- **Thermal**: battery ~40 C -> therm=1 caps (CPU 2354/2713 MHz, GPU 805 MHz).
- **Second phone**: per-device pins (GPT digest/CRC, Wi-Fi calibration digest, sensor calibration) are zero placeholders in this
  tree; the per-device pipeline is untested on another unit.

## Pieces in xnu/patches
Generated from the patch files and the q10jfinal44ts build log. "in q10jfinal44ts" = referenced by that build (pieces 0394+);
lower numbers were applied in the CI base kernel or superseded. "not individually verified" means the piece is part of a line
that ran on the phone, without a dedicated check recorded for it. Missing numbers: withheld (see the end of
`xnu/patches/SERIES.md` and docs/EXCLUDED.md) or never existed as a separate file.

| piece | name(s) | area | in q10jfinal44ts | status |
|---|---|---|---|---|
| 0001 | new-clang-drop-removed-auto-var-init-flag | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0002 | new-clang-backport-UIO_STACKBUF | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0003 | sweet-framebuffer-serial-console | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0004 | sweet-machine-config | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0005 | non-pac-build-fixes | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0006 | sweet-master-config-open-source-asm-pmap | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0007 | sweet-stub-apple-internal-headers | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0008 | sweet-fill-closed-lib-gaps-kpc-stub | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0009 | sweet-drop-apple-soc-only-exports | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0010 | sweet-early-framebuffer-debug | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0011 | sweet-physmap-alignment-padding | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0012 | sweet-keep-debug-console-after-ttbr0 | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0013 | sweet-kmem-rng-without-corecrypto | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0014 | sweet-kernel-hmac-prng | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0015 | sweet-unavailable-trust-cache-runtime | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0016 | sweet-device-tree-platform-expert | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0017 | sweet-device-tree-virtual-timer-irq | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0018 | sweet-boot-cpu-driver | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0019 | sweet-builtin-pthread | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0020 | sweet-portable-aes-cbc | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0021 | sweet-mockfs-ramdisk-root | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0022 | sweet-ramdisk-root-without-iomedia | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0023 | sweet-builtin-sha256-for-libkern | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0024 | sweet-mockfs-virtual-ramdisk-io | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0025 | sweet-builtin-amfi-no-entitlements | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0026 | sweet-builtin-hfs | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0027 | sweet-bearssl-md5 | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0028 | sweet-ufs-root-device | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0029 | sweet-unsealed-root-volume | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0030 | sweet-ufs-writes | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0031 | sweet-read-write-root | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0032 | sweet-bearssl-sha1 | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0033 | sweet-bearssl-sha384-sha512 | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0034 | sweet-ufs-dma-above-4g | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0035 | sweet-amfi-entitlements-platform-binaries | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0036 | sweet-amfi-cs-signed | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0037 | sweet-config-bindfs | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0038 | sweet-applekeystore | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0039 | sweet-coredump-default-off | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0040 | sweet-iowatchdog | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0041 | sweet-hfs-link-notes | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0042 | sweet-short-exec-log | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0043 | sweet-console-crash-log | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0044 | sweet-crash-log-reason | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0045 | sweet-iowatchdog-pmu-primary | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0046 | sweet-iosurface-iomfb | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0047 | sweet-persistent-console-log | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0048 | sweet-devnull-tee | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0049 | sweet-iokit-trace | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0050 | sweet-sandbox-policy | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0051 | sweet-crash-heap-strings | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0052 | sweet-hfs-rename-swap | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0053 | sweet-crash-scan-springboard | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0054 | sweet-process-sampler | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0055 | sweet-aks-ext-state-sampler-depth | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0056 | sweet-fb-throttled-present | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0057 | sweet-no-ext-resolver | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0058 | sweet-hid-event-services | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0059 | sweet-iphone-identity | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0060 | sweet-keys-touch-drivers | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0061 | sweet-iphone-x-display-size | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0062 | sweet-touch-power-battery | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0063 | sweet-goodix-geni-packing | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0064 | sweet-priv-work-interval | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0065 | sweet-smp-psci | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0066 | sweet-crash-fault-region | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0067 | sweet-console-mute | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0068 | sweet-surface-lookup-cached | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0069 | sweet-ufs-faster | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0070 | sweet-pid-hibernate | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0071 | sweet-touch-edge | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0072 | sweet-usb-probe | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0073 | sweet-usb-device-ep0 | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0074 | sweet-usb-vbus-override | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0075 | sweet-usb-ecm | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0076 | sweet-usb-rx-mbuf | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0077 | sweet-usb-live-log | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0078 | sweet-smp-8-cores | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0079 | sweet-display-power | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0080 | sweet-net-trace | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0081 | sweet-usb-link-state | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0082 | nord2-early-probe | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0083 | nord2-keystore-compat | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | base/superseded | historical (CI base or superseded) |
| 0086 | nord2-gpu-import-budget | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0087 | scope-cache-maintenance-to-job-buffers | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0088 | raise-private-budget-log-rejections | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0089 | kernel-job-timing-instrumentation | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0090 | low-latency-wait-and-async-wait-seq | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0091 | selectable-cold-start-gpu-opp | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0092 | gpu-job-interrupt-completion-optin | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0093 | stats-line-gpu-khz-battery-temp-irq | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0094 | lvts-thermal-sensing-optin | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0095 | short-boot-arg-aliases-n2lvts-n2mhz | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0096 | cpu-access-tracking-skip-gpu-only-maintenance | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0097 | framebuffer-present-thread-30hz-gpu-complete | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0098 | iosurface-plane-bpr-client-memory-selector-policy | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0099 | nord2-core-aware-sched-cpuclk | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0100 | nord2gpu-wait-without-connection-lock | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0101 | framebuffer-scan-address-probe | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0102 | framebuffer-swapwait-refresh-interval-retire-on-gpu | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0103 | p8-default-opp-654-ui-battery-guard-track2 | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0104 | gated-cpu-dvfs-big-clusters-n2cpu | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0105 | per-context-handles-1024 | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0106 | cpufreq-csram-diagnosis-governor-placement | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0108 | wait-seq-zero-is-a-successful-probe | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0109 | mcupm-readonly-probe-n2mcu | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0110 | scoped-invalidate-by-write-mask-n2wm | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0112 | display-surface-reservation | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0114 | mcupm-readonly-probe2-n2mcu2 | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0115 | mcupm-stage2-ping-n2mcuping | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0116 | cpu-dvfs-bringup-n2dvfs | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0117 | dvfs-preflight-rails-stock-rule | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0118 | dvfs-announce-flush-stages-two-rail-reads | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0119 | dvfs-skip-platform-init-stock-has-none | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0120 | cpu-memory-bandwidth-n2membw | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0121 | dram-dvfsrc-readonly-dump-n2dram | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0122 | gpu-hardware-counters-n2pf | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0123 | gpu-counters-v2-nonblocking-and-vm-stats | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0124 | vm-stats-only-n2pf-2 | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0125 | gpu-budgets-follow-ram-window | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0126 | gpu-memory-budgets-for-big-window | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0127 | gpu-real-clock-cycle-counter | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0128 | gpu-counters-v3-bounded-sample | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0129 | gpu-counters-v4-fault-aware | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0130 | gpu-counters-v5-dump-pointer-reset | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0131 | gpu-dispatch-latency-and-stats-lines | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0132 | gpu-job-irq-default-on | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0133 | fb-distinct-frame-counter | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0134 | gpu-hwinfo-selector | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0135 | gpu-alloc-gpu-only-and-write-combine | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0136 | gpu-alloc-extensions-boot-arg-and-stage-logs | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0137 | gpu-ext-stage-log-pauses | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0138 | gpu-full-list-maintenance-follows-tracking | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0139 | gpu-ext-defaults-no-pauses | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0140 | gpu-completion-interrupt-for-async-clients | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0141 | gpu-zero-fill-64k | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0142 | dvfs-starts-when-gpu-pmic-owner-ready | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0143 | gpu-alloc-outside-global-lock-and-timing | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0144 | gpu-alloc-without-connection-lock | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0145 | gpu-mmu-map-page-list | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0146 | system-telemetry-ufs-and-cpu-load | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0147 | dvfs-start-as-soon-as-pmic-owner-ready | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0148 | fb-busy-diagnostics | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0149 | fb-wait-for-inflight-swap | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0150 | fb-presenter-grace-wait | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0151 | fb-presenter-grace-8ms | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0152 | fb-arrival-and-display-regs-diag | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0153 | display-regs-cmdq-topckgen | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0154 | dsi-link-900mbps | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0155 | fb-event-driven-te-90hz | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0156 | fb-double-buffer-te-lead | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0157 | fb-prepare-thread-te-phase | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0158 | fb-underrun-accounting-precise-te | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0159 | te-tracker-panel-period | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0160 | te-reference-age-tegap-cpuclk | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0161 | arrival-phase-lead3ms | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0162 | arrival-fine-histogram | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0163 | frame-pacing-slot-centering | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0164 | flexible-lead-pacing-off | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0165 | minlead-1500-stats-1s | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0166 | process-cpu-telemetry | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0167 | slow-call-detector | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0168 | alloc-refusal-log | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0169 | reset-settle-persistent-log-gpu-mem | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0170 | log-ring-5ms-rgu-status | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0171 | thermal-limits-45-46-recover-44 | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0172 | cpu-load-scaling-governor | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0173 | breadcrumbs-stepwise-governor | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0174 | isr-breadcrumbs | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0175 | breadcrumb-after-records | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0176 | ram-window-read-scan | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0177 | ram-write-scan | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0178 | heartbeat | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0179 | emimpu-read | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0180 | proc-telemetry-off | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0181 | p57-reset-settle | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0182 | p57-refusal-slowcall-logring | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0183 | p57-governor-thermal | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0184 | p57-governor-only | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0185 | p57-release-reclaim | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0186 | handle-slots-4096 | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0187 | handle-slots-8192-retained-count | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0190 | te-locked-heartbeat-safe-window | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0191 | ovl-underrun-diagnostics | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0192 | copy-recheck-diagnostics | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0193 | top-layer-recheck | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0194 | left-edge-dump | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0195 | link-750mbps | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0196 | link-675-margin | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0197 | link-750-margin | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0198 | link-900-ns-margin | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0199 | link-750-more-margin | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0200 | amfi-dyld-policy | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0201 | diag-abort-locationd | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0202 | nord2-modem-readonly-probe | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0203 | nord2-modem-md1-power-ccif | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0204 | nord2-modem-region-sampling | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0205 | nord2-modem-safe-sampling | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0206 | nord2-modem-start-observe | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0207 | nord2-modem-start-observe-infracfg-base | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0208 | nord2-modem-start-smem-clear | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0209 | nord2-modem-start-smem-chunk-log | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0210 | nord2-modem-smem-readonly-walk | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0211 | nord2-modem-start-linux-region-clear | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0212 | nord2-modem-start-safe-regions-only | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0213 | nord2-modem-md1-power-cycle-linux-order | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0214 | nord2-modem-first-message-sram-dump | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0215 | nord2-modem-runtime-data-handshake | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0216 | nord2-modem-ccif-ring-queues | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0217 | nord2-modem-ring-init-page-log | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0218 | nord2-modem-rings-without-exception-rings | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0219 | nord2-modem-exp-rings-minimal-sram-tail | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0220 | nord2-modem-clear-cacheable-banks-wcomb | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0221 | nord2-modem-emi-mpu-dump | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0222 | nord2-modem-clear-ccb-dhl-only | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0223 | nord2-modem-single-word-probes | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0224 | nord2-modem-tight-power-cycle-late-start | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0225 | nord2-modem-full-linux-clears-exp-rings | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0226 | nord2-modem-step-pauses | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0227 | nord2-modem-step-pauses-no-exp-rings | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0228 | nord2-modem-full-linux-power-off-edge | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0229 | nord2-modem-smem-peek | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0230 | nord2-modem-smem-peek-paced | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0231 | nord2-modem-smem-peek-wcomb | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0232 | nord2-modem-emimpu-ap-region | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0233 | nord2-modem-device-mapped-bulk-clears | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0234 | nord2-modem-md1-off-first-all-clears | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0235 | nord2-modem-md1-off-clears-no-exp-rings | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0236 | nord2-modem-dense-ccif-md-side-log | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0237 | nord2-modem-ee-peek | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0238 | nord2-modem-dpmaif-hw-init | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0239 | nord2-modem-dpmaif-after-poweron | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0240 | nord2-modem-dpmaif-subset | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0241 | nord2-modem-dpmaif-noremap | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0242 | nord2-modem-dpmaif-noremap-noclears | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0243 | nord2-modem-exp-rings-ee-peek | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0244 | nord2-modem-exp-rings-fast | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0245 | nord2-modem-start-early | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0246 | nord2-autofastboot-loop | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0247 | nord2-modem-all-clears | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0248 | nord2-modem-rt-peek | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0249 | nord2-modem-mdside-ccif-log | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0250 | nord2-modem-readback | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0251 | nord2-modem-16-exp-rings | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0252 | nord2-modem-16-exp-rings-paced | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0253 | nord2-modem-16-exp-rings-nogap | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0254 | nord2-modem-rings-headers-only | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0255 | nord2-modem-prep-in-off-window-ccci-service | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0256 | nord2-modem-no-ccif-reset | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0257 | nord2-modem-16exp-ccci-paced | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0258 | nord2-modem-exp-ring-locate | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0259 | nord2-modem-rings-after-bootup | platform: display/touch/battery/modem bring-up | base/superseded | historical (CI base or superseded) |
| 0264 | nord2-modem-q10ak-16exp | modem (CCCI/DPMAIF, AT client) | base/superseded | historical (CI base or superseded) |
| 0285 | nord2-applog-applog_name | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0286 | nord2-applog-report-fd-null | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0287 | nord2-applog-amfi-dev-env-vars | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0288 | nord2-sandbox-extension-issue-consume | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0289 | nord2-camera-stall-format4-fb-alignment | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0290 | nord2-camera-focus-enqueue-spacing-front30 | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0291 | nord2-hwmodel-per-process | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0292 | nord2-camera-two-instances | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0293 | nord2-camera-gain-code-per-sensor | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0294 | nord2-camera-fair-lock-hw-time | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0296 | nord2-autotest | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0297 | nord2-fghw-apps | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0298 | nord2-hwcodec-jpeg | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0299 | nord2-jpegdev-vcodec, nord2-surface-budget-report | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0300 | nord2-surface-holders-report | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0310 | nord2-jpegdev-vcodec | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0311 | nord2-vcodec-venc-autotest | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0312 | nord2-vcodec-venc-fast | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0313 | nord2-vcodec-pool-384m | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0314 | nord2-vcodec-adaptive-wait | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0315 | nord2-jpeg-hw-decode-path | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0316 | nord2-jpeg-ioctl-kernel-buffer | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0317 | nord2-jpeg-encode-eoi-trim-128 | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0321 | nord2-mic-devfs-worker | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0330 | nord2-audio-cam-power-not-a-refusal | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0331 | nord2-cam-fan-i2c8-per-transaction-lease | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0340 | nord2-camera-sensor-modes-60 | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0350 | nord2-gnss-gpsdl-core | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0351 | nord2-gnss-gpsdl-bind | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0352 | nord2-gnss-datalink | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0353 | nord2-gnss-safe-steps | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0354 | nord2-gnss-single-access | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0355 | nord2-gnss-map-kernel-map | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0356 | nord2-gnss-ladder | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0357 | nord2-gnss-ladder-l1 | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0358 | nord2-gnss-l1-probe | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0359 | nord2-gnss-datalink-ladder | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0360 | nord2-surface-client-wrap | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0361 | nord2-gpu-import-check-flushmem | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0362 | nord2-gnss-datalink-v9 | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0363 | nord2-gnss-split-ladders | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0364 | nord2-gnss-dsp-boot | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | base/superseded | historical (CI base or superseded) |
| 0390 | nord2-fb-layer-blend | display IOMMU, HW composition, framebuffer pacing, colour | base/superseded | historical (CI base or superseded) |
| 0391 | nord2-fb-layer-inval-diag | display IOMMU, HW composition, framebuffer pacing, colour | base/superseded | historical (CI base or superseded) |
| 0392 | nord2-fb-swap-mask-retain | display IOMMU, HW composition, framebuffer pacing, colour | base/superseded | historical (CI base or superseded) |
| 0393 | nord2-fb-sync | display IOMMU, HW composition, framebuffer pacing, colour | base/superseded | historical (CI base or superseded) |
| 0394 | nord2-fb-hw-pool | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0395 | nord2-gpu-pool-fence | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0396 | nord2-fb-hw-overlay-compose | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0397 | nord2-fb-pace90 | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0398 | nord2-iosurface-use-count | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0399 | nord2-fb-hw-colour-order | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0400 | nord2-gpu-latency | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0401 | nord2-fb-display-iommu | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0402 | nord2-fb-hw-multi-layer-b, nord2-fb-hw-multi-layer-c, nord2-fb-hw-multi-layer | display IOMMU, HW composition, framebuffer pacing, colour | no | superseded / experiment / not merged |
| 0403 | nord2-fb-iommu-staged-arming | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0404 | nord2-fb-hw-test-poweroff-on0403, nord2-fb-hw-test-poweroff | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0405 | nord2-dvfs-floor-l-2200 | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0406 | nord2-fb-pool-reclaim | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0407 | nord2-fps-meter-on0403, nord2-fps-meter | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0408 | nord2-fb-iommu-init-order | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0409 | nord2-fb-iommu-substeps | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0410 | nord2-iomap-kernel-map | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0411 | nord2-fb-iommu-default-on | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0413 | nord2-fb-no-cpu-copy | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0414 | nord2-fb-hw-colour-pm0 | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0415 | nord2-fb-hw-cascade-4l | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0416 | nord2-fb-overlay-only | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0417 | nord2-mem-surface-purge | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0418 | nord2-heap-probe, nord2-mem-ram-scaled-budgets | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0419 | nord2-heap-probe | display IOMMU, HW composition, framebuffer pacing, colour | no | superseded / experiment / not merged |
| 0420 | nord2-memgov | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the phone-run final line; not individually verified |
| 0430 | nord2-wifi-ios-join-scan-policy | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0431 | nord2-wifi-DIAG-q10jgw-scan-refusal | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | no | superseded / experiment / not merged |
| 0432 | nord2-wifi-sae-group19 | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0433 | nord2-wifi-sae-akm-pmf-station | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0434 | nord2-wifi-ios-wpa3-join | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0436 | nord2-wifi-private-address | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0440 | nord2-gfx-heap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0441 | nord2-gpu-heap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0450 | nord2-wifi-8021x-stack | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0451 | nord2-wifi-8021x-supplicant-client | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0452 | nord2-wifi-8021x-ptk-rekey | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0453 | nord2-wifi-8021x-handshake-message | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0460 | nord2-dvfs-sched-governor | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0461 | nord2-gpu-runtime-dvfs | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0462 | nord2-dram-rate-probe | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0470 | nord2-fb-iommu-early-arm | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0480 | nord2-surface-wire-on-demand | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0481 | nord2-gpu-madvise-purge | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0482 | nord2-vm-compressor-no-swap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0483 | nord2-wod-not-pool-heap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0484 | nord2-memgov-app-class-budget | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0490 | nord2-fb-yuv422-overlay | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0491 | nord2-wifi-private-address-fw-sequence | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0492 | nord2-gpu-dvfs-ui-profile | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0493 | nord2-dram-rate-bootarg-watch | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | no | superseded / experiment / not merged |
| 0494 | nord2-dram-probe-kernel-map | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0495 | nord2-display-backlight | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0496 | nord2-torch-mt6360 | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the phone-run final line; not individually verified |
| 0500 | nord2-gpu-dynamic-as | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0501 | nord2-backlight-legacy | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0502 | nord2-torch-capability | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0530 | nord2-clock-floor | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0531 | nord2-scp-motion-off | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0540 | nord2-mt6359-rtc | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0550 | nord2-fb-punch-hole-status-bar | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0551 | nord2-fb-cutout-off-by-default | memory, scheduler, camera flash | yes | in the phone-run final line; not individually verified |
| 0560 | nord2-camera-flash-strobe | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0580 | nord2-gnss-port | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0581 | nord2-gnss-pipe-guard | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0582 | nord2-gnss-l1-lna | GNSS (pipe, EMI layout, LNA, bytewise ring) | no | superseded / experiment / not merged |
| 0583 | nord2-gnss-persistent-trace | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0584 | nord2-gnss-emi-vendor-layout | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0585 | nord2-gnss-l1-lna-on-vendor-layout | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0586 | nord2-gnss-libmnl-boots-dsp | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0587 | nord2-gnss-pacing-probe | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0588 | nord2-gnss-hold-like-s11 | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0589 | nord2-gnss-quiet-pipe | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0590 | nord2-gnss-tx-no-poll | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0591 | nord2-gnss-xchg-ioctl-cpu-trace | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the phone-run final line; not individually verified |
| 0592 | nord2-gnss-ring-bytewise | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | verified (GNSS fix) |
| 0593 | nord2-gnss-lna-ioctl | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | verified (GNSS fix) |
| 0610 | nord2-aw8697-haptics.on-0560, nord2-aw8697-haptics | haptics (AW8697) | yes | in the phone-run final line; not individually verified |
| 0611 | nord2-aw8697-ram-clicks | haptics (AW8697) | yes | in the phone-run final line; not individually verified |
| 0612 | nord2-haptic-soft, nord2-haptic-strong | haptics (AW8697) | no | superseded / experiment / not merged |
| 0613 | nord2-haptic-train-paths | haptics (AW8697) | yes | in the phone-run final line; not individually verified |
| 0614 | nord2-haptic-lookahead | haptics (AW8697) | yes | in the phone-run final line; not individually verified |
| 0615 | nord2-haptic-strong | haptics (AW8697) | no | superseded / experiment / not merged |
| 0616 | nord2-haptic-short-profiles | haptics (AW8697) | yes | in the phone-run final line; not individually verified |
| 0617 | nord2-haptic-v4a, nord2-haptic-v4b, nord2-haptic-v4c | haptics (AW8697) | no | superseded / experiment / not merged |
| 0618 | nord2-haptic-fixed-gain-6a, nord2-haptic-fixed-gain-6b, nord2-haptic-fixed-gain-6c | haptics (AW8697) | yes | verified (haptic feel, user) |
| 0620 | nord2-touch-pacing | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0621 | nord2-fb-jank-events | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0630 | nord2-gpu-jsnext-pipelining | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0631 | nord2-gpu-fast-ramp-dvfs | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0632 | nord2-gpu-pass-overlap | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0633 | nord2-gpu-886-live-voltage | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0641 | nord2-cpu-platform-parity | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0642 | nord2-prime-first-placement | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0643 | nord2-soc-thermal-ladder | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the phone-run final line; not individually verified |
| 0650 | nord2-ram8-heap-regions | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | verified (heap 1324 MiB) |
| 0651 | nord2-ram8-heap-check-on | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | verified (pool 1577 MiB with 0653) |
| 0652 | nord2-ram8c-drivers-extra-ram | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0653 | nord2-ram8-heap-check-dt-guard | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | verified (pool 1577 MiB) |
| 0655 | nord2-ram8c-heap-lowbank-off | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0656 | nord2-ram8c-heap-top-lowbank-off | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0658 | nord2-ram8c-console-tail | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0660 | nord2-gpu-stepA-telemetry-maint-prio-irq | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | verified (GPU step A) |
| 0664 | nord2-gpu-l2-flush-policy | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | discarded (faults) |
| 0665 | nord2-gpu-opp-voltage-core-gating | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0667 | nord2-gpu-l2-wc-invalidate | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | discarded |
| 0668 | nord2-gpu-opp-voltage-core-gating-noL2 | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | verified (chain flag fix) |
| 0670 | nord2-mdp-probe-l2, nord2-mdp-probe | MDP offload | yes | verified (MDP probe) |
| 0671 | nord2-mdp-stages | MDP offload | yes | verified (MDP S1/S2) |
| 0672 | nord2-m2scaler-service | MDP offload | yes | verified (M2Scaler service) |
| 0673 | nord2-mdp-pwron-stage | MDP offload | yes | in the phone-run final line; not individually verified |
| 0674 | nord2-mdp-larb2-diag | MDP offload | yes | in the phone-run final line; not individually verified |
| 0675 | nord2-mdp-larb2-check-fix | MDP offload | yes | in the phone-run final line; not individually verified |
| 0676 | nord2-mdp-vendor-rdma-wrot-words | MDP offload | yes | in the phone-run final line; not individually verified |
| 0677 | nord2-mdp-wrot-rotation-offsets | MDP offload | yes | in the phone-run final line; not individually verified |
| 0678 | nord2-mdp-s2-nv12-rsz | MDP offload | yes | in the phone-run final line; not individually verified |
| 0679 | nord2-mdp-aal-tdshp-tile-sizes | MDP offload | yes | in the phone-run final line; not individually verified |
| 0680 | nord2-mdp-tdshp-enable | MDP offload | yes | in the phone-run final line; not individually verified |
| 0681 | nord2-mdp-scale-single-tile-limit | MDP offload | yes | verified |
| 0682 | nord2-mdp-s2-diag-scale-jobs-s3on, nord2-mdp-s2-diag-scale-jobs | MDP offload | yes | in the phone-run final line; not individually verified |
| 0683 | nord2-mdp-ntile-rgbscale-s3-svc | MDP offload | yes | verified (multi-tile) |
| 0684 | nord2-m2scaler-controlled-run | MDP offload | yes | verified (service on) |
| 0685 | nord2-m2scaler-request-decoder | MDP offload | yes | in the phone-run final line; not individually verified |
| 0686 | nord2-mdp-nv12-dst-timing | MDP offload | yes | host/phone (see log) |
| 0687 | nord2-mdp-svc-speed-iova-cache | MDP offload | yes | in the phone-run final line; not individually verified |
| 0688 | nord2-mdp-svc-surface-cache-clock-log | MDP offload | yes | in the phone-run final line; not individually verified |
| 0689 | nord2-mdp-svc-timing-rotation-jobs | MDP offload | yes | in the phone-run final line; not individually verified |
| 0690 | nord2-mdp-dual-pipe-svc-timing | MDP offload | yes | in the phone-run final line; not individually verified |
| 0691 | nord2-mdp-recover-scale-policy | MDP offload | yes | in the phone-run final line; not individually verified |
| 0700 | nord2-usb-boot-without-host | USB at boot, battery gauge, charging | yes | in phone-run line; no dedicated check recorded |
| 0701 | nord2-battery-gauge-report | USB at boot, battery gauge, charging | yes | in phone-run line; no dedicated check recorded |
| 0702 | nord2-mp2650-charge-control | USB at boot, battery gauge, charging | yes | in phone-run line; no dedicated check recorded |
| 0703 | nord2-vbus-fast-edge | USB at boot, battery gauge, charging | yes | in phone-run line; no dedicated check recorded |
| 0710 | nord2-sim-pin-state | SIM, carrier, NR experiment, modem NV blob source | yes | in phone-run line; no dedicated check recorded |
| 0711 | nord2-sim-generic-apn | SIM, carrier, NR experiment, modem NV blob source | yes | in phone-run line; no dedicated check recorded |
| 0712 | nord2-nr-reporting | SIM, carrier, NR experiment, modem NV blob source | yes | in phone-run line; no dedicated check recorded |
| 0713 | nord2-simcache-helper-sysctls | SIM, carrier, NR experiment, modem NV blob source | yes | in phone-run line; no dedicated check recorded |
| 0714 | nord2-nr-ui-experiment | SIM, carrier, NR experiment, modem NV blob source | yes | experiment, default off |
| 0715 | nord2-modem-nv-blob-rootfs-file | SIM, carrier, NR experiment, modem NV blob source | yes | ran on phone (Settings check not recorded) |
| 0720 | nord2-bt-controller | Bluetooth controller/transport/registry | yes | verified (BT up, A2DP AAC) |
| 0721 | nord2-bt-transport | Bluetooth controller/transport/registry | yes | verified (BT up, A2DP AAC) |
| 0722 | nord2-bt-registry | Bluetooth controller/transport/registry | yes | verified (BT up, A2DP AAC) |
| 0730 | nord2-sandbox-container-path-appex | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | verified (picker no longer crashes) |
| 0731 | nord2-sandbox-fileprovider-log | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | log only |
| 0732 | nord2-memfloor-lsd | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | in phone-run line; no dedicated check recorded |
| 0733 | nord2-hfs-apfs-fsctl | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | built; never triggered (inert) |
| 0734 | nord2-root-apfs-typename | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | verified (On My iPhone, user) |
| 0750 | nord2-gpu-mmu-fault-record-revive | GPU fault recovery | yes | in phone-run line; no dedicated check recorded |
| 0760 | nord2-scp-sensors-ios | sensors (motion, orientation, light) | yes | verified (accelerometer streams) |
| 0761 | nord2-scp-orientation-light | sensors (motion, orientation, light) | yes | verified (auto-rotate, user) |
| 0762 | nord2-scp-light-summary | sensors (motion, orientation, light) | yes | in phone-run line; no dedicated check recorded |
| 0790 | nord2-wifi-rssi-bounds | Wi-Fi auto-join RSSI bounds | yes | verified (auto-join ~32-37 s) |
