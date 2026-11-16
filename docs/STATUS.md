# Port status: kernel lines, pieces, known limits

**verified on one phone** = confirmed by the user on the author's phone in the 2026-11-15/16 sessions. Everything else carries
no verification claim here; the engineering log (`notes/ENGINEERING-LOG.md`) has the evidence of earlier milestones. Image
hashes are sha256 prefixes of the author's per-device images (not reproducible from this tree: they contain the author's pins).

## Verified on one phone (final line q10jfinal44ts, sha256 f55a15ea)
| Area | Result | Pieces / payloads |
|---|---|---|
| MDP | dual pipe works on Magnifier | 0690 (+ MDP chain 0670-0691) |
| Charging | charging fix works | 0700-0703 |
| Modem / SIM | modem firmware restored; Carrier row clean; SIM works | 0715 + nvfile1, simcache1, 0710-0712 (PIN/APN) |
| 5G | not possible in iOS (iPhone X Mav17 baseband capability) | 0714 experiment off |
| GPU | Terraria landscape + touch fixed; GPU fault recovery in the line | gfxall17 + flip; 0750 |
| Sensors | accelerometer/gyro/compass data and auto-rotate work; **Compass app frozen (open)**; **ambient light silent (open)** | 0760, 0761 (0762 in the line) |
| Bluetooth | pairing and AAC streaming stable in transport mode 2; early start at ~24 s with Wi-Fi (S0e); **long-run and airplane-mode boot tests open** | 0720-0722 + n2btd |
| TrollStore | opens; Install IPA File picker opens; ipainbox installed Terraria + Subway | 0730 sandbox container, 0731 SBLOG, 0732 jetsam floor (fixed lsd kill + TrollStore registration), fix2 payload re-registers |
| Files | "On My iPhone" fixed | 0734 (root statfs type "apfs" for fileproviderd); 0733 never fired, kept harmless |
| Logs | fplog1 readable-log payload | optional |
| Torch/flash | not claimed | - |

## Kernel lines (most recent first; only hash prefixes)
| Line | sha256 prefix | Adds |
|---|---|---|
| q10jfinal44ts | f55a15ea | 0734 root type name "apfs" for fileproviderd (kern.n2fsapfs) — **on the phone** |
| q10jfinal43ts | 8f91f1a6 | 0733 HFS answers APFS 'J' fsctls (kern.n2hfsapfs) |
| q10jfinal42ts | - | 0732 memory-limit floor for lsd/installd/pkd/fileproviderd/containermanagerd |
| q10jfinal41ts | 1fce7efb | 0731 sandbox/AMFI log for the FileProvider stack |
| q10jfinal40ts | 5a1b2684 | 0730 sandbox op 4 container path for .appex processes |
| q10jfinal40bt | 866531b0 | BT bring-up at boot after en0 (S0e) |
| q10jfinal39bt .. 30bt | 7a8c5a3a, 1fc698b9, 011c5a70, 4f13091e, 8d736dce, 0de54731, ba190162, ea42c6a9, eafee24f, d9c6bf5b | BT iterations bt10..bt21 (transport mode 2 default from 39bt) |
| q10jfinal30 / 27 / 25 | 3301f088 / 28ed2227 / 66eb02b1 | sensors 0762 / 0761 / 0760 |
| q10jfinal29bt .. 23bt | 853e5d66, 27e5706b, b54f3da2, 18cb3fa2, b679dde6 | BT bt5..bt9 |
| q10jfinal21 | - | 0750 GPU fault recovery |
| q10jfinal19 | 65bc8d2b | 0715 modem blob from a root-volume file |
| q10jfinal18 / 18bt2 | d63ae64b / 7d444f2e | SIM 0710-0714 + BT 0720-0722 |
| q10jfinal16sim2 | 7c4fbe0b | SIM/carrier 0710-0714 |
| q10jfinal14usb | d73e8f99 | USB/battery/charging 0700-0702 |
| q10jfinal13 | 2d7a82b5 | MDP chain to 0688 + Wi-Fi auto-join 0790 |
| q10jfinal8a | 69f0991e | GPU steps A/C, 0668, RAM8 heap 0650, MDP 0670-0681 |
| q10jperf6f | 719fcd83 | GPU step A + C |
| q10hapd6a | 5d2cf34f | haptic fixed gain (0618) |

## Known limits / open
- **5G**: not possible in iOS — the capability is keyed on the iPhone X (Mav17) baseband identity.
- **Compass app** frozen; **ambient light** silent.
- **Bluetooth**: long-run and airplane-mode boot tests open; no HFP/SCO, no HID input.
- **Memory**: DRAM above the XNU window not usable as normal RAM; memory pressure kills daemons (0732 mitigates).
- **App Store**: genuine install does not work (FairPlay).
- **Second phone**: untested; per-device pins are zero placeholders in this tree.
- **Kernel build from this repo**: not attempted (CI object archive, XNU tarball and toolchain not included; see README).

## Pieces in xnu/patches
Generated from the patch files and the q10jfinal44ts build log. "in q10jfinal44ts" = referenced by that build (pieces 0394+);
lower numbers were applied in the CI base kernel or superseded. Missing numbers: withheld (modem NVRAM, see the end of
`xnu/patches/SERIES.md`) or never a separate file.

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
| 0084 | nord2-gpu | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0085 | sweet-fb-surface-retention | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
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
| 0111 | iokit-trap-fast-path | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0112 | display-surface-reservation | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0113 | transport-trap-block-yields-to-frontend | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
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
| 0188 | kernel-stress-clients | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
| 0189 | stress-bootarg-n2st | GPU kernel driver (Nord2GPU), framebuffer, power, stress | base/superseded | historical (CI base or superseded) |
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
| 0281 | call-audio-bb-hooks | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
| 0282 | audio-install-upgrade | telephony UI, proximity, ringtone, autotest, hwcodec base | base/superseded | historical (CI base or superseded) |
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
| 0394 | nord2-fb-hw-pool | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0395 | nord2-gpu-pool-fence | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0396 | nord2-fb-hw-overlay-compose | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0397 | nord2-fb-pace90 | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0398 | nord2-iosurface-use-count | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0399 | nord2-fb-hw-colour-order | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0400 | nord2-gpu-latency | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0401 | nord2-fb-display-iommu | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0402 | nord2-fb-hw-multi-layer-b, nord2-fb-hw-multi-layer-c, nord2-fb-hw-multi-layer | display IOMMU, HW composition, framebuffer pacing, colour | no | superseded / experiment / not merged |
| 0403 | nord2-fb-iommu-staged-arming | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0404 | nord2-fb-hw-test-poweroff-on0403, nord2-fb-hw-test-poweroff | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0405 | nord2-dvfs-floor-l-2200 | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0406 | nord2-fb-pool-reclaim | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0407 | nord2-fps-meter-on0403, nord2-fps-meter | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0408 | nord2-fb-iommu-init-order | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0409 | nord2-fb-iommu-substeps | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0410 | nord2-iomap-kernel-map | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0411 | nord2-fb-iommu-default-on | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0413 | nord2-fb-no-cpu-copy | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0414 | nord2-fb-hw-colour-pm0 | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0415 | nord2-fb-hw-cascade-4l | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0416 | nord2-fb-overlay-only | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0417 | nord2-mem-surface-purge | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0418 | nord2-heap-probe, nord2-mem-ram-scaled-budgets | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0419 | nord2-heap-probe | display IOMMU, HW composition, framebuffer pacing, colour | no | superseded / experiment / not merged |
| 0420 | nord2-memgov | display IOMMU, HW composition, framebuffer pacing, colour | yes | in the final line; no individual claim (see engineering log) |
| 0430 | nord2-wifi-ios-join-scan-policy | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0431 | nord2-wifi-DIAG-q10jgw-scan-refusal | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | no | superseded / experiment / not merged |
| 0432 | nord2-wifi-sae-group19 | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0433 | nord2-wifi-sae-akm-pmf-station | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0434 | nord2-wifi-ios-wpa3-join | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0436 | nord2-wifi-private-address | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0440 | nord2-gfx-heap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0441 | nord2-gpu-heap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0450 | nord2-wifi-8021x-stack | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0451 | nord2-wifi-8021x-supplicant-client | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0452 | nord2-wifi-8021x-ptk-rekey | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0453 | nord2-wifi-8021x-handshake-message | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0460 | nord2-dvfs-sched-governor | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0461 | nord2-gpu-runtime-dvfs | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0462 | nord2-dram-rate-probe | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0470 | nord2-fb-iommu-early-arm | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0480 | nord2-surface-wire-on-demand | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0481 | nord2-gpu-madvise-purge | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0482 | nord2-vm-compressor-no-swap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0483 | nord2-wod-not-pool-heap | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0484 | nord2-memgov-app-class-budget | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0490 | nord2-fb-yuv422-overlay | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0491 | nord2-wifi-private-address-fw-sequence | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0492 | nord2-gpu-dvfs-ui-profile | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0493 | nord2-dram-rate-bootarg-watch | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | no | superseded / experiment / not merged |
| 0494 | nord2-dram-probe-kernel-map | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0495 | nord2-display-backlight | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0496 | nord2-torch-mt6360 | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | in the final line; no individual claim (see engineering log) |
| 0500 | nord2-gpu-dynamic-as | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0501 | nord2-backlight-legacy | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0502 | nord2-torch-capability | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0530 | nord2-clock-floor | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0531 | nord2-scp-motion-off | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0540 | nord2-mt6359-rtc | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0550 | nord2-fb-punch-hole-status-bar | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0551 | nord2-fb-cutout-off-by-default | memory, scheduler, camera flash | yes | in the final line; no individual claim (see engineering log) |
| 0560 | nord2-camera-flash-strobe | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0580 | nord2-gnss-port | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0581 | nord2-gnss-pipe-guard | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0582 | nord2-gnss-l1-lna | GNSS (pipe, EMI layout, LNA, bytewise ring) | no | superseded / experiment / not merged |
| 0583 | nord2-gnss-persistent-trace | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0584 | nord2-gnss-emi-vendor-layout | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0585 | nord2-gnss-l1-lna-on-vendor-layout | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0586 | nord2-gnss-libmnl-boots-dsp | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0587 | nord2-gnss-pacing-probe | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0588 | nord2-gnss-hold-like-s11 | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0589 | nord2-gnss-quiet-pipe | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0590 | nord2-gnss-tx-no-poll | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0591 | nord2-gnss-xchg-ioctl-cpu-trace | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0592 | nord2-gnss-ring-bytewise | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0593 | nord2-gnss-lna-ioctl | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | in the final line; no individual claim (see engineering log) |
| 0610 | nord2-aw8697-haptics.on-0560, nord2-aw8697-haptics | haptics (AW8697) | yes | in the final line; no individual claim (see engineering log) |
| 0611 | nord2-aw8697-ram-clicks | haptics (AW8697) | yes | in the final line; no individual claim (see engineering log) |
| 0612 | nord2-haptic-soft, nord2-haptic-strong | haptics (AW8697) | no | superseded / experiment / not merged |
| 0613 | nord2-haptic-train-paths | haptics (AW8697) | yes | in the final line; no individual claim (see engineering log) |
| 0614 | nord2-haptic-lookahead | haptics (AW8697) | yes | in the final line; no individual claim (see engineering log) |
| 0615 | nord2-haptic-strong | haptics (AW8697) | no | superseded / experiment / not merged |
| 0616 | nord2-haptic-short-profiles | haptics (AW8697) | yes | in the final line; no individual claim (see engineering log) |
| 0617 | nord2-haptic-v4a, nord2-haptic-v4b, nord2-haptic-v4c | haptics (AW8697) | no | superseded / experiment / not merged |
| 0618 | nord2-haptic-fixed-gain-6a, nord2-haptic-fixed-gain-6b, nord2-haptic-fixed-gain-6c | haptics (AW8697) | yes | in the final line; no individual claim (see engineering log) |
| 0620 | nord2-touch-pacing | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0621 | nord2-fb-jank-events | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0630 | nord2-gpu-jsnext-pipelining | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0631 | nord2-gpu-fast-ramp-dvfs | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0632 | nord2-gpu-pass-overlap | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0633 | nord2-gpu-886-live-voltage | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0641 | nord2-cpu-platform-parity | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0642 | nord2-prime-first-placement | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0643 | nord2-soc-thermal-ladder | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0650 | nord2-ram8-heap-regions | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0651 | nord2-ram8-heap-check-on | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0652 | nord2-ram8c-drivers-extra-ram | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0653 | nord2-ram8-heap-check-dt-guard | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0655 | nord2-ram8c-heap-lowbank-off | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0656 | nord2-ram8c-heap-top-lowbank-off | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0658 | nord2-ram8c-console-tail | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0660 | nord2-gpu-stepA-telemetry-maint-prio-irq | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0664 | nord2-gpu-l2-flush-policy | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0665 | nord2-gpu-opp-voltage-core-gating | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0667 | nord2-gpu-l2-wc-invalidate | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no | superseded / experiment / not merged |
| 0668 | nord2-gpu-opp-voltage-core-gating-noL2 | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | in the final line; no individual claim (see engineering log) |
| 0670 | nord2-mdp-probe-l2, nord2-mdp-probe | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0671 | nord2-mdp-stages | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0672 | nord2-m2scaler-service | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0673 | nord2-mdp-pwron-stage | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0674 | nord2-mdp-larb2-diag | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0675 | nord2-mdp-larb2-check-fix | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0676 | nord2-mdp-vendor-rdma-wrot-words | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0677 | nord2-mdp-wrot-rotation-offsets | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0678 | nord2-mdp-s2-nv12-rsz | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0679 | nord2-mdp-aal-tdshp-tile-sizes | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0680 | nord2-mdp-tdshp-enable | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0681 | nord2-mdp-scale-single-tile-limit | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0682 | nord2-mdp-s2-diag-scale-jobs-s3on, nord2-mdp-s2-diag-scale-jobs | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0683 | nord2-mdp-ntile-rgbscale-s3-svc | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0684 | nord2-m2scaler-controlled-run | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0685 | nord2-m2scaler-request-decoder | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0686 | nord2-mdp-nv12-dst-timing | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0687 | nord2-mdp-svc-speed-iova-cache | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0688 | nord2-mdp-svc-surface-cache-clock-log | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0689 | nord2-mdp-svc-timing-rotation-jobs | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0690 | nord2-mdp-dual-pipe-svc-timing | MDP offload | yes | verified on one phone (MDP dual pipe on Magnifier) |
| 0691 | nord2-mdp-recover-scale-policy | MDP offload | yes | in the final line; no individual claim (see engineering log) |
| 0700 | nord2-usb-boot-without-host | USB at boot, battery gauge, charging | yes | verified on one phone (charging fix) |
| 0701 | nord2-battery-gauge-report | USB at boot, battery gauge, charging | yes | verified on one phone (charging fix) |
| 0702 | nord2-mp2650-charge-control | USB at boot, battery gauge, charging | yes | verified on one phone (charging fix) |
| 0703 | nord2-vbus-fast-edge | USB at boot, battery gauge, charging | yes | verified on one phone (charging fix) |
| 0710 | nord2-sim-pin-state | SIM, carrier, NR experiment, modem NV blob source | yes | verified on one phone (SIM PIN state; Carrier row clean) |
| 0711 | nord2-sim-generic-apn | SIM, carrier, NR experiment, modem NV blob source | yes | verified on one phone (APN; SIM works) |
| 0712 | nord2-nr-reporting | SIM, carrier, NR experiment, modem NV blob source | yes | verified on one phone (part of the SIM set); 5G not possible in iOS |
| 0713 | nord2-simcache-helper-sysctls | SIM, carrier, NR experiment, modem NV blob source | yes | verified on one phone (simcache helper; Carrier row clean) |
| 0714 | nord2-nr-ui-experiment | SIM, carrier, NR experiment, modem NV blob source | yes | experiment, default off (5G not possible in iOS) |
| 0715 | nord2-modem-nv-blob-rootfs-file | SIM, carrier, NR experiment, modem NV blob source | yes | verified on one phone (modem firmware restored) |
| 0720 | nord2-bt-controller | Bluetooth controller/transport/registry | yes | verified on one phone (pairing, AAC stable in transport mode 2; long-run/airplane boot open) |
| 0721 | nord2-bt-transport | Bluetooth controller/transport/registry | yes | verified on one phone (pairing, AAC stable in transport mode 2) |
| 0722 | nord2-bt-registry | Bluetooth controller/transport/registry | yes | verified on one phone (early start ~24 s, S0e) |
| 0730 | nord2-sandbox-container-path-appex | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | verified on one phone (picker opens, TrollStore installs) |
| 0731 | nord2-sandbox-fileprovider-log | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | verified on one phone (SBLOG logging) |
| 0732 | nord2-memfloor-lsd | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | verified on one phone (fixed lsd kill + TrollStore registration) |
| 0733 | nord2-hfs-apfs-fsctl | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | never fired; kept harmless |
| 0734 | nord2-root-apfs-typename | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | verified on one phone (Files "On My iPhone") |
| 0750 | nord2-gpu-mmu-fault-record-revive | GPU fault recovery | yes | in the final line (GPU fault recovery) |
| 0760 | nord2-scp-sensors-ios | sensors (motion, orientation, light) | yes | verified on one phone (acc/gyro/compass data) |
| 0761 | nord2-scp-orientation-light | sensors (motion, orientation, light) | yes | verified on one phone (auto-rotate) |
| 0762 | nord2-scp-light-summary | sensors (motion, orientation, light) | yes | in the line; ambient light still silent (open) |
| 0790 | nord2-wifi-rssi-bounds | Wi-Fi auto-join RSSI bounds | yes | in the final line; no individual claim (see engineering log) |
