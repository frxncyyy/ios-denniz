# Kernel patch series of the Nord 2 port

One file per piece (`NNNN-name.patch`, unified diff against the SWEET/XNU tree of ios-sweet). Where several versions of a
piece existed in the work tree, the file is the one the final build (`q10jfinal44ts`) used (`identical` in its rebase log) or
else the newest copy. Pieces 0394..0790 marked *final* are referenced by the q10jfinal44ts build log; pieces below 0394 were
applied in the CI base kernel or superseded [inferred: the CI series itself is not in this tree]. Rebased (`regenerated`)
pieces differ from the file here only by context; the exact compiled result is `xnu/stage-q10jfinal44ts/`.

| piece | file | area | final line | origin dir | note |
|---|---|---|---|---|---|
| 0001 | `0001-new-clang-drop-removed-auto-var-init-flag.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0002 | `0002-new-clang-backport-UIO_STACKBUF.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0003 | `0003-sweet-framebuffer-serial-console.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0004 | `0004-sweet-machine-config.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0005 | `0005-non-pac-build-fixes.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0006 | `0006-sweet-master-config-open-source-asm-pmap.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0007 | `0007-sweet-stub-apple-internal-headers.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0008 | `0008-sweet-fill-closed-lib-gaps-kpc-stub.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0009 | `0009-sweet-drop-apple-soc-only-exports.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0010 | `0010-sweet-early-framebuffer-debug.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0011 | `0011-sweet-physmap-alignment-padding.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0012 | `0012-sweet-keep-debug-console-after-ttbr0.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0013 | `0013-sweet-kmem-rng-without-corecrypto.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0014 | `0014-sweet-kernel-hmac-prng.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0015 | `0015-sweet-unavailable-trust-cache-runtime.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0016 | `0016-sweet-device-tree-platform-expert.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0017 | `0017-sweet-device-tree-virtual-timer-irq.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0018 | `0018-sweet-boot-cpu-driver.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0019 | `0019-sweet-builtin-pthread.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0020 | `0020-sweet-portable-aes-cbc.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0021 | `0021-sweet-mockfs-ramdisk-root.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0022 | `0022-sweet-ramdisk-root-without-iomedia.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0023 | `0023-sweet-builtin-sha256-for-libkern.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0024 | `0024-sweet-mockfs-virtual-ramdisk-io.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0025 | `0025-sweet-builtin-amfi-no-entitlements.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0026 | `0026-sweet-builtin-hfs.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0027 | `0027-sweet-bearssl-md5.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0028 | `0028-sweet-ufs-root-device.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0029 | `0029-sweet-unsealed-root-volume.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0030 | `0030-sweet-ufs-writes.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0031 | `0031-sweet-read-write-root.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0032 | `0032-sweet-bearssl-sha1.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0033 | `0033-sweet-bearssl-sha384-sha512.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0034 | `0034-sweet-ufs-dma-above-4g.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0035 | `0035-sweet-amfi-entitlements-platform-binaries.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0036 | `0036-sweet-amfi-cs-signed.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0037 | `0037-sweet-config-bindfs.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0038 | `0038-sweet-applekeystore.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0039 | `0039-sweet-coredump-default-off.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0040 | `0040-sweet-iowatchdog.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0041 | `0041-sweet-hfs-link-notes.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0042 | `0042-sweet-short-exec-log.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0043 | `0043-sweet-console-crash-log.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0044 | `0044-sweet-crash-log-reason.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0045 | `0045-sweet-iowatchdog-pmu-primary.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0046 | `0046-sweet-iosurface-iomfb.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0047 | `0047-sweet-persistent-console-log.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0048 | `0048-sweet-devnull-tee.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0049 | `0049-sweet-iokit-trace.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0050 | `0050-sweet-sandbox-policy.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0051 | `0051-sweet-crash-heap-strings.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0052 | `0052-sweet-hfs-rename-swap.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0053 | `0053-sweet-crash-scan-springboard.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0054 | `0054-sweet-process-sampler.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0055 | `0055-sweet-aks-ext-state-sampler-depth.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0056 | `0056-sweet-fb-throttled-present.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0057 | `0057-sweet-no-ext-resolver.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0058 | `0058-sweet-hid-event-services.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0059 | `0059-sweet-iphone-identity.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0060 | `0060-sweet-keys-touch-drivers.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0061 | `0061-sweet-iphone-x-display-size.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0062 | `0062-sweet-touch-power-battery.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0063 | `0063-sweet-goodix-geni-packing.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0064 | `0064-sweet-priv-work-interval.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0065 | `0065-sweet-smp-psci.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0066 | `0066-sweet-crash-fault-region.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0067 | `0067-sweet-console-mute.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0068 | `0068-sweet-surface-lookup-cached.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0069 | `0069-sweet-ufs-faster.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0070 | `0070-sweet-pid-hibernate.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0071 | `0071-sweet-touch-edge.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0072 | `0072-sweet-usb-probe.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0073 | `0073-sweet-usb-device-ep0.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0074 | `0074-sweet-usb-vbus-override.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0075 | `0075-sweet-usb-ecm.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0076 | `0076-sweet-usb-rx-mbuf.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0077 | `0077-sweet-usb-live-log.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0078 | `0078-sweet-smp-8-cores.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0079 | `0079-sweet-display-power.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0080 | `0080-sweet-net-trace.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0081 | `0081-sweet-usb-link-state.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0082 | `0082-nord2-early-probe.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0083 | `0083-nord2-keystore-compat.patch` | early bring-up (SWEET shim era: UFS, display, input, Wi-Fi, audio) | pre-0394 base/superseded | W-xnu |  |
| 0086 | `0086-nord2-gpu-import-budget.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0087 | `0087-scope-cache-maintenance-to-job-buffers.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0088 | `0088-raise-private-budget-log-rejections.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0089 | `0089-kernel-job-timing-instrumentation.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0090 | `0090-low-latency-wait-and-async-wait-seq.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0091 | `0091-selectable-cold-start-gpu-opp.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0092 | `0092-gpu-job-interrupt-completion-optin.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0093 | `0093-stats-line-gpu-khz-battery-temp-irq.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0094 | `0094-lvts-thermal-sensing-optin.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0095 | `0095-short-boot-arg-aliases-n2lvts-n2mhz.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0096 | `0096-cpu-access-tracking-skip-gpu-only-maintenance.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0097 | `0097-framebuffer-present-thread-30hz-gpu-complete.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0098 | `0098-iosurface-plane-bpr-client-memory-selector-policy.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0099 | `0099-nord2-core-aware-sched-cpuclk.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | ansuz-port-20261026 |  |
| 0100 | `0100-nord2gpu-wait-without-connection-lock.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0101 | `0101-framebuffer-scan-address-probe.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0102 | `0102-framebuffer-swapwait-refresh-interval-retire-on-gpu.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0103 | `0103-p8-default-opp-654-ui-battery-guard-track2.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0104 | `0104-gated-cpu-dvfs-big-clusters-n2cpu.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0105 | `0105-per-context-handles-1024.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0106 | `0106-cpufreq-csram-diagnosis-governor-placement.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0108 | `0108-wait-seq-zero-is-a-successful-probe.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0109 | `0109-mcupm-readonly-probe-n2mcu.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0110 | `0110-scoped-invalidate-by-write-mask-n2wm.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0112 | `0112-display-surface-reservation.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0114 | `0114-mcupm-readonly-probe2-n2mcu2.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0115 | `0115-mcupm-stage2-ping-n2mcuping.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0116 | `0116-cpu-dvfs-bringup-n2dvfs.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0117 | `0117-dvfs-preflight-rails-stock-rule.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0118 | `0118-dvfs-announce-flush-stages-two-rail-reads.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0119 | `0119-dvfs-skip-platform-init-stock-has-none.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0120 | `0120-cpu-memory-bandwidth-n2membw.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0121 | `0121-dram-dvfsrc-readonly-dump-n2dram.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0122 | `0122-gpu-hardware-counters-n2pf.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0123 | `0123-gpu-counters-v2-nonblocking-and-vm-stats.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0124 | `0124-vm-stats-only-n2pf-2.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0125 | `0125-gpu-budgets-follow-ram-window.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0126 | `0126-gpu-memory-budgets-for-big-window.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0127 | `0127-gpu-real-clock-cycle-counter.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0128 | `0128-gpu-counters-v3-bounded-sample.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0129 | `0129-gpu-counters-v4-fault-aware.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0130 | `0130-gpu-counters-v5-dump-pointer-reset.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0131 | `0131-gpu-dispatch-latency-and-stats-lines.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0132 | `0132-gpu-job-irq-default-on.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0133 | `0133-fb-distinct-frame-counter.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0134 | `0134-gpu-hwinfo-selector.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0135 | `0135-gpu-alloc-gpu-only-and-write-combine.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0136 | `0136-gpu-alloc-extensions-boot-arg-and-stage-logs.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0137 | `0137-gpu-ext-stage-log-pauses.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0138 | `0138-gpu-full-list-maintenance-follows-tracking.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0139 | `0139-gpu-ext-defaults-no-pauses.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0140 | `0140-gpu-completion-interrupt-for-async-clients.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0141 | `0141-gpu-zero-fill-64k.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0142 | `0142-dvfs-starts-when-gpu-pmic-owner-ready.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0143 | `0143-gpu-alloc-outside-global-lock-and-timing.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0144 | `0144-gpu-alloc-without-connection-lock.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0145 | `0145-gpu-mmu-map-page-list.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0146 | `0146-system-telemetry-ufs-and-cpu-load.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0147 | `0147-dvfs-start-as-soon-as-pmic-owner-ready.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0148 | `0148-fb-busy-diagnostics.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0149 | `0149-fb-wait-for-inflight-swap.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0150 | `0150-fb-presenter-grace-wait.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0151 | `0151-fb-presenter-grace-8ms.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0152 | `0152-fb-arrival-and-display-regs-diag.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0153 | `0153-display-regs-cmdq-topckgen.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0154 | `0154-dsi-link-900mbps.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0155 | `0155-fb-event-driven-te-90hz.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0156 | `0156-fb-double-buffer-te-lead.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0157 | `0157-fb-prepare-thread-te-phase.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0158 | `0158-fb-underrun-accounting-precise-te.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0159 | `0159-te-tracker-panel-period.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0160 | `0160-te-reference-age-tegap-cpuclk.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0161 | `0161-arrival-phase-lead3ms.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0162 | `0162-arrival-fine-histogram.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0163 | `0163-frame-pacing-slot-centering.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0164 | `0164-flexible-lead-pacing-off.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0165 | `0165-minlead-1500-stats-1s.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0166 | `0166-process-cpu-telemetry.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0167 | `0167-slow-call-detector.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0168 | `0168-alloc-refusal-log.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0169 | `0169-reset-settle-persistent-log-gpu-mem.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0170 | `0170-log-ring-5ms-rgu-status.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0171 | `0171-thermal-limits-45-46-recover-44.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0172 | `0172-cpu-load-scaling-governor.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0173 | `0173-breadcrumbs-stepwise-governor.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0174 | `0174-isr-breadcrumbs.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0175 | `0175-breadcrumb-after-records.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0176 | `0176-ram-window-read-scan.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0177 | `0177-ram-write-scan.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0178 | `0178-heartbeat.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0179 | `0179-emimpu-read.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0180 | `0180-proc-telemetry-off.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0181 | `0181-p57-reset-settle.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0182 | `0182-p57-refusal-slowcall-logring.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0183 | `0183-p57-governor-thermal.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0184 | `0184-p57-governor-only.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0185 | `0185-p57-release-reclaim.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0186 | `0186-handle-slots-4096.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0187 | `0187-handle-slots-8192-retained-count.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0190 | `0190-te-locked-heartbeat-safe-window.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0191 | `0191-ovl-underrun-diagnostics.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0192 | `0192-copy-recheck-diagnostics.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0193 | `0193-top-layer-recheck.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0194 | `0194-left-edge-dump.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0195 | `0195-link-750mbps.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0196 | `0196-link-675-margin.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0197 | `0197-link-750-margin.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0198 | `0198-link-900-ns-margin.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0199 | `0199-link-750-more-margin.patch` | GPU kernel driver (Nord2GPU), framebuffer, power, stress | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0200 | `0200-amfi-dyld-policy.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0201 | `0201-diag-abort-locationd.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0202 | `0202-nord2-modem-readonly-probe.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0203 | `0203-nord2-modem-md1-power-ccif.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0204 | `0204-nord2-modem-region-sampling.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0205 | `0205-nord2-modem-safe-sampling.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0206 | `0206-nord2-modem-start-observe.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0207 | `0207-nord2-modem-start-observe-infracfg-base.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0208 | `0208-nord2-modem-start-smem-clear.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0209 | `0209-nord2-modem-start-smem-chunk-log.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0210 | `0210-nord2-modem-smem-readonly-walk.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0211 | `0211-nord2-modem-start-linux-region-clear.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0212 | `0212-nord2-modem-start-safe-regions-only.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0213 | `0213-nord2-modem-md1-power-cycle-linux-order.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0214 | `0214-nord2-modem-first-message-sram-dump.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0215 | `0215-nord2-modem-runtime-data-handshake.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0216 | `0216-nord2-modem-ccif-ring-queues.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0217 | `0217-nord2-modem-ring-init-page-log.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0218 | `0218-nord2-modem-rings-without-exception-rings.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0219 | `0219-nord2-modem-exp-rings-minimal-sram-tail.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0220 | `0220-nord2-modem-clear-cacheable-banks-wcomb.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0221 | `0221-nord2-modem-emi-mpu-dump.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0222 | `0222-nord2-modem-clear-ccb-dhl-only.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0223 | `0223-nord2-modem-single-word-probes.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0224 | `0224-nord2-modem-tight-power-cycle-late-start.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0225 | `0225-nord2-modem-full-linux-clears-exp-rings.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0226 | `0226-nord2-modem-step-pauses.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0227 | `0227-nord2-modem-step-pauses-no-exp-rings.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0228 | `0228-nord2-modem-full-linux-power-off-edge.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0229 | `0229-nord2-modem-smem-peek.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0230 | `0230-nord2-modem-smem-peek-paced.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0231 | `0231-nord2-modem-smem-peek-wcomb.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0232 | `0232-nord2-modem-emimpu-ap-region.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0233 | `0233-nord2-modem-device-mapped-bulk-clears.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0234 | `0234-nord2-modem-md1-off-first-all-clears.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0235 | `0235-nord2-modem-md1-off-clears-no-exp-rings.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0236 | `0236-nord2-modem-dense-ccif-md-side-log.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0237 | `0237-nord2-modem-ee-peek.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0238 | `0238-nord2-modem-dpmaif-hw-init.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0239 | `0239-nord2-modem-dpmaif-after-poweron.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0240 | `0240-nord2-modem-dpmaif-subset.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0241 | `0241-nord2-modem-dpmaif-noremap.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0242 | `0242-nord2-modem-dpmaif-noremap-noclears.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0243 | `0243-nord2-modem-exp-rings-ee-peek.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0244 | `0244-nord2-modem-exp-rings-fast.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0245 | `0245-nord2-modem-start-early.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0246 | `0246-nord2-autofastboot-loop.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0247 | `0247-nord2-modem-all-clears.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0248 | `0248-nord2-modem-rt-peek.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0249 | `0249-nord2-modem-mdside-ccif-log.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0250 | `0250-nord2-modem-readback.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0251 | `0251-nord2-modem-16-exp-rings.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0252 | `0252-nord2-modem-16-exp-rings-paced.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0253 | `0253-nord2-modem-16-exp-rings-nogap.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0254 | `0254-nord2-modem-rings-headers-only.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0255 | `0255-nord2-modem-prep-in-off-window-ccci-service.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0256 | `0256-nord2-modem-no-ccif-reset.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0257 | `0257-nord2-modem-16exp-ccci-paced.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0258 | `0258-nord2-modem-exp-ring-locate.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0259 | `0259-nord2-modem-rings-after-bootup.patch` | platform: display/touch/battery/modem bring-up | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0264 | `0264-nord2-modem-q10ak-16exp.patch` | modem (CCCI/DPMAIF, AT client) | pre-0394 base/superseded | kernel-p1-20261026 |  |
| 0285 | `0285-nord2-applog-applog_name.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | applog-20261110 |  |
| 0286 | `0286-nord2-applog-report-fd-null.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | applog-20261110b |  |
| 0287 | `0287-nord2-applog-amfi-dev-env-vars.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | applog-20261110c |  |
| 0288 | `0288-nord2-sandbox-extension-issue-consume.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | sandboxext-20261110 |  |
| 0289 | `0289-nord2-camera-stall-format4-fb-alignment.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | camfix-20261110 |  |
| 0290 | `0290-nord2-camera-focus-enqueue-spacing-front30.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | camfocus-20261110 |  |
| 0291 | `0291-nord2-hwmodel-per-process.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | hwmodel-20261110 |  |
| 0292 | `0292-nord2-camera-two-instances.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | camdual-20261110 |  |
| 0293 | `0293-nord2-camera-gain-code-per-sensor.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | camgain-20261110 |  |
| 0294 | `0294-nord2-camera-fair-lock-hw-time.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | camfair-20261110 |  |
| 0296 | `0296-nord2-autotest.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | autotest |  |
| 0297 | `0297-nord2-fghw-apps.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | autotest |  |
| 0298 | `0298-nord2-hwcodec-jpeg.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | hwcodec |  |
| 0299 | `0299-nord2-jpegdev-vcodec.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | hwcodec |  |
| 0299 | `0299-nord2-surface-budget-report.patch` | telephony UI, proximity, ringtone, autotest, hwcodec base | pre-0394 base/superseded | surfbudget-20261111 |  |
| 0300 | `0300-nord2-surface-holders-report.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | surfholders-20261111 |  |
| 0310 | `0310-nord2-jpegdev-vcodec.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0311 | `0311-nord2-vcodec-venc-autotest.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0312 | `0312-nord2-vcodec-venc-fast.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0313 | `0313-nord2-vcodec-pool-384m.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0314 | `0314-nord2-vcodec-adaptive-wait.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0315 | `0315-nord2-jpeg-hw-decode-path.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0316 | `0316-nord2-jpeg-ioctl-kernel-buffer.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0317 | `0317-nord2-jpeg-encode-eoi-trim-128.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | hwcodec |  |
| 0321 | `0321-nord2-mic-devfs-worker.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | audio-capture-20261111 |  |
| 0330 | `0330-nord2-audio-cam-power-not-a-refusal.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | i2c8cam-20261111 |  |
| 0331 | `0331-nord2-cam-fan-i2c8-per-transaction-lease.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | i2c8cam-20261111 |  |
| 0340 | `0340-nord2-camera-sensor-modes-60.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | cam60-20261112 |  |
| 0350 | `0350-nord2-gnss-gpsdl-core.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-20261112 |  |
| 0351 | `0351-nord2-gnss-gpsdl-bind.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-20261112 |  |
| 0352 | `0352-nord2-gnss-datalink.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-m2-20261112 |  |
| 0353 | `0353-nord2-gnss-safe-steps.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-safe-20261112 |  |
| 0354 | `0354-nord2-gnss-single-access.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-safe2-20261112 |  |
| 0355 | `0355-nord2-gnss-map-kernel-map.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-safe3-20261112 |  |
| 0356 | `0356-nord2-gnss-ladder.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-ladder-20261112 |  |
| 0357 | `0357-nord2-gnss-ladder-l1.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-ladder2-20261112 |  |
| 0358 | `0358-nord2-gnss-l1-probe.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-probe-20261112 |  |
| 0359 | `0359-nord2-gnss-datalink-ladder.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-dl-20261112 |  |
| 0360 | `0360-nord2-surface-client-wrap.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | clientsurf-20261112 |  |
| 0361 | `0361-nord2-gpu-import-check-flushmem.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gpuread-20261112 |  |
| 0362 | `0362-nord2-gnss-datalink-v9.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-dl2-20261112 |  |
| 0363 | `0363-nord2-gnss-split-ladders.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-dl3-20261112 |  |
| 0364 | `0364-nord2-gnss-dsp-boot.patch` | hardware codecs (JPEG, H.264/HEVC), camera sensor modes | pre-0394 base/superseded | gnss-dl4-20261112 |  |
| 0390 | `0390-nord2-fb-layer-blend.patch` | display IOMMU, HW composition, framebuffer pacing, colour | pre-0394 base/superseded | fbblend-20261112 |  |
| 0391 | `0391-nord2-fb-layer-inval-diag.patch` | display IOMMU, HW composition, framebuffer pacing, colour | pre-0394 base/superseded | fbdiag-20261112 |  |
| 0392 | `0392-nord2-fb-swap-mask-retain.patch` | display IOMMU, HW composition, framebuffer pacing, colour | pre-0394 base/superseded | fbretain-20261112 |  |
| 0393 | `0393-nord2-fb-sync.patch` | display IOMMU, HW composition, framebuffer pacing, colour | pre-0394 base/superseded | fbsync-20261112 |  |
| 0394 | `0394-nord2-fb-hw-pool.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | hwcomp-20261112 |  |
| 0395 | `0395-nord2-gpu-pool-fence.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | hwcomp-20261112 |  |
| 0396 | `0396-nord2-fb-hw-overlay-compose.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | hwcomp-20261112 |  |
| 0397 | `0397-nord2-fb-pace90.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | fbpace-20261112 |  |
| 0398 | `0398-nord2-iosurface-use-count.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | usecount-20261112-on0397 |  |
| 0399 | `0399-nord2-fb-hw-colour-order.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | hwcolor-20261112-on0398 |  |
| 0400 | `0400-nord2-gpu-latency.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | gpulat-20261112 |  |
| 0401 | `0401-nord2-fb-display-iommu.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | iommu-20261112 |  |
| 0402 | `0402-nord2-fb-hw-multi-layer-b.patch` | display IOMMU, HW composition, framebuffer pacing, colour | no (superseded or not merged) | hwmulti-20261113-on0401b |  |
| 0402 | `0402-nord2-fb-hw-multi-layer-c.patch` | display IOMMU, HW composition, framebuffer pacing, colour | no (superseded or not merged) | hwmulti-20261113-on0401c |  |
| 0402 | `0402-nord2-fb-hw-multi-layer.patch` | display IOMMU, HW composition, framebuffer pacing, colour | no (superseded or not merged) | hwmulti-20261113-on0401 |  |
| 0403 | `0403-nord2-fb-iommu-staged-arming.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | iommu-20261113b |  |
| 0404 | `0404-nord2-fb-hw-test-poweroff-on0403.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | hwfix-20261113-on0403 |  |
| 0404 | `0404-nord2-fb-hw-test-poweroff.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | hwfix-20261113 |  |
| 0405 | `0405-nord2-dvfs-floor-l-2200.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | gov-20261113 |  |
| 0406 | `0406-nord2-fb-pool-reclaim.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | poolreclaim-20261114 |  |
| 0407 | `0407-nord2-fps-meter-on0403.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | fpsmeter-20261114-on0403 |  |
| 0407 | `0407-nord2-fps-meter.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | fpsmeter-20261114 |  |
| 0408 | `0408-nord2-fb-iommu-init-order.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | iommuinit-20261113 |  |
| 0409 | `0409-nord2-fb-iommu-substeps.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | iommusub-20261113 |  |
| 0410 | `0410-nord2-iomap-kernel-map.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | iomapfix-20261113 |  |
| 0411 | `0411-nord2-fb-iommu-default-on.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | iommuboot-20261113 |  |
| 0413 | `0413-nord2-fb-no-cpu-copy.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | nocpu-20261113 |  |
| 0414 | `0414-nord2-fb-hw-colour-pm0.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | colorfix-20261113 |  |
| 0415 | `0415-nord2-fb-hw-cascade-4l.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | hwmulti4l-20261113 |  |
| 0416 | `0416-nord2-fb-overlay-only.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | nocpu2-20261113 |  |
| 0417 | `0417-nord2-mem-surface-purge.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | memfix-20261113 |  |
| 0418 | `0418-nord2-heap-probe.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | heapprobe-20261113 |  |
| 0418 | `0418-nord2-mem-ram-scaled-budgets.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | memfix-20261113b |  |
| 0419 | `0419-nord2-heap-probe.patch` | display IOMMU, HW composition, framebuffer pacing, colour | no (superseded or not merged) | heapprobe-20261113b |  |
| 0420 | `0420-nord2-memgov.patch` | display IOMMU, HW composition, framebuffer pacing, colour | yes | integ-20261114 |  |
| 0430 | `0430-nord2-wifi-ios-join-scan-policy.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0431 | `0431-nord2-wifi-DIAG-q10jgw-scan-refusal.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | no (superseded or not merged) | wifi-20261113 |  |
| 0432 | `0432-nord2-wifi-sae-group19.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0433 | `0433-nord2-wifi-sae-akm-pmf-station.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0434 | `0434-nord2-wifi-ios-wpa3-join.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0436 | `0436-nord2-wifi-private-address.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0440 | `0440-nord2-gfx-heap.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0441 | `0441-nord2-gpu-heap.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0450 | `0450-nord2-wifi-8021x-stack.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0451 | `0451-nord2-wifi-8021x-supplicant-client.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0452 | `0452-nord2-wifi-8021x-ptk-rekey.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0453 | `0453-nord2-wifi-8021x-handshake-message.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0460 | `0460-nord2-dvfs-sched-governor.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0461 | `0461-nord2-gpu-runtime-dvfs.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0462 | `0462-nord2-dram-rate-probe.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0470 | `0470-nord2-fb-iommu-early-arm.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0480 | `0480-nord2-surface-wire-on-demand.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0481 | `0481-nord2-gpu-madvise-purge.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0482 | `0482-nord2-vm-compressor-no-swap.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0483 | `0483-nord2-wod-not-pool-heap.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0484 | `0484-nord2-memgov-app-class-budget.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0490 | `0490-nord2-fb-yuv422-overlay.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | integ-20261114 |  |
| 0491 | `0491-nord2-wifi-private-address-fw-sequence.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | wifi-fix-20261114 |  |
| 0492 | `0492-nord2-gpu-dvfs-ui-profile.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | ccperf-20261114 |  |
| 0493 | `0493-nord2-dram-rate-bootarg-watch.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | no (superseded or not merged) | ccperf-20261114 |  |
| 0494 | `0494-nord2-dram-probe-kernel-map.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | gpuas-20261114 |  |
| 0495 | `0495-nord2-display-backlight.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | bright-fix-20261114 |  |
| 0496 | `0496-nord2-torch-mt6360.patch` | Wi-Fi (SAE/WPA3, 802.1X, scan, power) | yes | bright-fix-20261114 |  |
| 0500 | `0500-nord2-gpu-dynamic-as.patch` | memory, scheduler, camera flash | yes | gpuas-20261114 |  |
| 0501 | `0501-nord2-backlight-legacy.patch` | memory, scheduler, camera flash | yes | bltorch-20261114 |  |
| 0502 | `0502-nord2-torch-capability.patch` | memory, scheduler, camera flash | yes | bltorch-20261114 |  |
| 0530 | `0530-nord2-clock-floor.patch` | memory, scheduler, camera flash | yes | heat-20261114 |  |
| 0531 | `0531-nord2-scp-motion-off.patch` | memory, scheduler, camera flash | yes | heat-20261114 |  |
| 0540 | `0540-nord2-mt6359-rtc.patch` | memory, scheduler, camera flash | yes | rtc-20261114 |  |
| 0550 | `0550-nord2-fb-punch-hole-status-bar.patch` | memory, scheduler, camera flash | yes | cutout-20261114 |  |
| 0551 | `0551-nord2-fb-cutout-off-by-default.patch` | memory, scheduler, camera flash | yes | sbshift-20261114 |  |
| 0560 | `0560-nord2-camera-flash-strobe.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0560-nord2-camera-flash-strobe.patch |
| 0580 | `0580-nord2-gnss-port.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | 3-way merged, regenerated (rebased onto the stage) |
| 0581 | `0581-nord2-gnss-pipe-guard.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0581-nord2-gnss-pipe-guard.patch |
| 0582 | `0582-nord2-gnss-l1-lna.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | no (superseded or not merged) | gnss-20261114 |  |
| 0583 | `0583-nord2-gnss-persistent-trace.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0583-nord2-gnss-persistent-trace.patch |
| 0584 | `0584-nord2-gnss-emi-vendor-layout.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0584-nord2-gnss-emi-vendor-layout.patch |
| 0585 | `0585-nord2-gnss-l1-lna-on-vendor-layout.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0585-nord2-gnss-l1-lna-on-vendor-layout.patch |
| 0586 | `0586-nord2-gnss-libmnl-boots-dsp.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0586-nord2-gnss-libmnl-boots-dsp.patch |
| 0587 | `0587-nord2-gnss-pacing-probe.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0587-nord2-gnss-pacing-probe.patch |
| 0588 | `0588-nord2-gnss-hold-like-s11.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0588-nord2-gnss-hold-like-s11.patch |
| 0589 | `0589-nord2-gnss-quiet-pipe.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0589-nord2-gnss-quiet-pipe.patch |
| 0590 | `0590-nord2-gnss-tx-no-poll.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0590-nord2-gnss-tx-no-poll.patch |
| 0591 | `0591-nord2-gnss-xchg-ioctl-cpu-trace.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0591-nord2-gnss-xchg-ioctl-cpu-trace.patch |
| 0592 | `0592-nord2-gnss-ring-bytewise.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0592-nord2-gnss-ring-bytewise.patch |
| 0593 | `0593-nord2-gnss-lna-ioctl.patch` | GNSS (pipe, EMI layout, LNA, bytewise ring) | yes | jbts-20261115 | identical; file xnu/patches/0593-nord2-gnss-lna-ioctl.patch |
| 0610 | `0610-nord2-aw8697-haptics.on-0560.patch` | haptics (AW8697) | yes | camflash-20261114 | 3-way merged, regenerated (rebased onto the stage) |
| 0610 | `0610-nord2-aw8697-haptics.patch` | haptics (AW8697) | yes | jbts-20261115 | 3-way merged, regenerated (rebased onto the stage) |
| 0611 | `0611-nord2-aw8697-ram-clicks.patch` | haptics (AW8697) | yes | jbts-20261115 | identical; file xnu/patches/0611-nord2-aw8697-ram-clicks.patch |
| 0612 | `0612-nord2-haptic-soft.patch` | haptics (AW8697) | no (superseded or not merged) | haptic2-20261114 |  |
| 0612 | `0612-nord2-haptic-strong.patch` | haptics (AW8697) | no (superseded or not merged) | haptic2-20261114 |  |
| 0613 | `0613-nord2-haptic-train-paths.patch` | haptics (AW8697) | yes | jbts-20261115 | identical; file xnu/patches/0613-nord2-haptic-train-paths.patch |
| 0614 | `0614-nord2-haptic-lookahead.patch` | haptics (AW8697) | yes | jbts-20261115 | identical; file xnu/patches/0614-nord2-haptic-lookahead.patch |
| 0615 | `0615-nord2-haptic-strong.patch` | haptics (AW8697) | no (superseded or not merged) | haptic4-20261114 |  |
| 0616 | `0616-nord2-haptic-short-profiles.patch` | haptics (AW8697) | yes | jbts-20261115 | identical; file xnu/patches/0616-nord2-haptic-short-profiles.patch |
| 0617 | `0617-nord2-haptic-v4a.patch` | haptics (AW8697) | no (superseded or not merged) | haptic5-20261114 |  |
| 0617 | `0617-nord2-haptic-v4b.patch` | haptics (AW8697) | no (superseded or not merged) | haptic5-20261114 |  |
| 0617 | `0617-nord2-haptic-v4c.patch` | haptics (AW8697) | no (superseded or not merged) | haptic5-20261114 |  |
| 0618 | `0618-nord2-haptic-fixed-gain-6a.patch` | haptics (AW8697) | yes | jbts-20261115 | identical; file xnu/patches/0618-nord2-haptic-fixed-gain-6a.patch |
| 0618 | `0618-nord2-haptic-fixed-gain-6b.patch` | haptics (AW8697) | yes | haptic7-20261114 | identical; file xnu/patches/0618-nord2-haptic-fixed-gain-6a.patch |
| 0618 | `0618-nord2-haptic-fixed-gain-6c.patch` | haptics (AW8697) | yes | haptic7-20261114 | identical; file xnu/patches/0618-nord2-haptic-fixed-gain-6a.patch |
| 0620 | `0620-nord2-touch-pacing.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0620-nord2-touch-pacing.patch |
| 0621 | `0621-nord2-fb-jank-events.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | 3-way merged, regenerated (rebased onto the stage) |
| 0630 | `0630-nord2-gpu-jsnext-pipelining.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0630-nord2-gpu-jsnext-pipelining.patch |
| 0631 | `0631-nord2-gpu-fast-ramp-dvfs.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0631-nord2-gpu-fast-ramp-dvfs.patch |
| 0632 | `0632-nord2-gpu-pass-overlap.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0632-nord2-gpu-pass-overlap.patch |
| 0633 | `0633-nord2-gpu-886-live-voltage.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0633-nord2-gpu-886-live-voltage.patch |
| 0641 | `0641-nord2-cpu-platform-parity.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0641-nord2-cpu-platform-parity.patch |
| 0642 | `0642-nord2-prime-first-placement.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0642-nord2-prime-first-placement.patch |
| 0643 | `0643-nord2-soc-thermal-ladder.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | 3-way merged, regenerated (rebased onto the stage) |
| 0650 | `0650-nord2-ram8-heap-regions.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0650-nord2-ram8-heap-regions.patch |
| 0651 | `0651-nord2-ram8-heap-check-on.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0651-nord2-ram8-heap-check-on.patch |
| 0652 | `0652-nord2-ram8c-drivers-extra-ram.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no (superseded or not merged) | cpu-20261114 |  |
| 0653 | `0653-nord2-ram8-heap-check-dt-guard.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0653-nord2-ram8-heap-check-dt-guard.patch |
| 0655 | `0655-nord2-ram8c-heap-lowbank-off.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no (superseded or not merged) | gpusched-20261114 |  |
| 0656 | `0656-nord2-ram8c-heap-top-lowbank-off.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no (superseded or not merged) | gpusched-20261114 |  |
| 0658 | `0658-nord2-ram8c-console-tail.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no (superseded or not merged) | gpusched-20261114 |  |
| 0660 | `0660-nord2-gpu-stepA-telemetry-maint-prio-irq.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | identical; file xnu/patches/0660-nord2-gpu-stepA-telemetry-maint-prio-irq.patch |
| 0664 | `0664-nord2-gpu-l2-flush-policy.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no (superseded or not merged) | mdp-20261114 |  |
| 0665 | `0665-nord2-gpu-opp-voltage-core-gating.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no (superseded or not merged) | mdp-20261114 |  |
| 0667 | `0667-nord2-gpu-l2-wc-invalidate.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | no (superseded or not merged) | gpusched-20261114 |  |
| 0668 | `0668-nord2-gpu-opp-voltage-core-gating-noL2.patch` | touch pacing, CPU/GPU DVFS, thermal, RAM8 graphics heap | yes | jbts-20261115 | regenerated (rebased onto the stage) |
| 0670 | `0670-nord2-mdp-probe-l2.patch` | MDP offload | yes | mdp-20261114 | regenerated (rebased onto the stage) |
| 0670 | `0670-nord2-mdp-probe.patch` | MDP offload | yes | jbts-20261115 | regenerated (rebased onto the stage) |
| 0671 | `0671-nord2-mdp-stages.patch` | MDP offload | yes | jbts-20261115 | regenerated (rebased onto the stage) |
| 0672 | `0672-nord2-m2scaler-service.patch` | MDP offload | yes | jbts-20261115 | 3-way merged, regenerated (rebased onto the stage) |
| 0673 | `0673-nord2-mdp-pwron-stage.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0673-nord2-mdp-pwron-stage.patch |
| 0674 | `0674-nord2-mdp-larb2-diag.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0674-nord2-mdp-larb2-diag.patch |
| 0675 | `0675-nord2-mdp-larb2-check-fix.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0675-nord2-mdp-larb2-check-fix.patch |
| 0676 | `0676-nord2-mdp-vendor-rdma-wrot-words.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0676-nord2-mdp-vendor-rdma-wrot-words.patch |
| 0677 | `0677-nord2-mdp-wrot-rotation-offsets.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0677-nord2-mdp-wrot-rotation-offsets.patch |
| 0678 | `0678-nord2-mdp-s2-nv12-rsz.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0678-nord2-mdp-s2-nv12-rsz.patch |
| 0679 | `0679-nord2-mdp-aal-tdshp-tile-sizes.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0679-nord2-mdp-aal-tdshp-tile-sizes.patch |
| 0680 | `0680-nord2-mdp-tdshp-enable.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0680-nord2-mdp-tdshp-enable.patch |
| 0681 | `0681-nord2-mdp-scale-single-tile-limit.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0681-nord2-mdp-scale-single-tile-limit.patch |
| 0682 | `0682-nord2-mdp-s2-diag-scale-jobs-s3on.patch` | MDP offload | yes | mdp-20261114 | identical; file xnu/patches/0682-nord2-mdp-s2-diag-scale-jobs.patch |
| 0682 | `0682-nord2-mdp-s2-diag-scale-jobs.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0682-nord2-mdp-s2-diag-scale-jobs.patch |
| 0683 | `0683-nord2-mdp-ntile-rgbscale-s3-svc.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0683-nord2-mdp-ntile-rgbscale-s3-svc.patch |
| 0684 | `0684-nord2-m2scaler-controlled-run.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0684-nord2-m2scaler-controlled-run.patch |
| 0685 | `0685-nord2-m2scaler-request-decoder.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0685-nord2-m2scaler-request-decoder.patch |
| 0686 | `0686-nord2-mdp-nv12-dst-timing.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0686-nord2-mdp-nv12-dst-timing.patch |
| 0687 | `0687-nord2-mdp-svc-speed-iova-cache.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0687-nord2-mdp-svc-speed-iova-cache.patch |
| 0688 | `0688-nord2-mdp-svc-surface-cache-clock-log.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0688-nord2-mdp-svc-surface-cache-clock-log.patch |
| 0689 | `0689-nord2-mdp-svc-timing-rotation-jobs.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0689-nord2-mdp-svc-timing-rotation-jobs.patch |
| 0690 | `0690-nord2-mdp-dual-pipe-svc-timing.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0690-nord2-mdp-dual-pipe-svc-timing.patch |
| 0691 | `0691-nord2-mdp-recover-scale-policy.patch` | MDP offload | yes | jbts-20261115 | identical; file xnu/patches/0691-nord2-mdp-recover-scale-policy.patch |
| 0700 | `0700-nord2-usb-boot-without-host.patch` | USB at boot, battery gauge, charging | yes | jbts-20261115 | identical; file xnu/patches/0700-nord2-usb-boot-without-host.patch |
| 0701 | `0701-nord2-battery-gauge-report.patch` | USB at boot, battery gauge, charging | yes | jbts-20261115 | identical; file xnu/patches/0701-nord2-battery-gauge-report.patch |
| 0702 | `0702-nord2-mp2650-charge-control.patch` | USB at boot, battery gauge, charging | yes | jbts-20261115 | identical; file xnu/patches/0702-nord2-mp2650-charge-control.patch |
| 0703 | `0703-nord2-vbus-fast-edge.patch` | USB at boot, battery gauge, charging | yes | jbts-20261115 | identical; file xnu/patches/0703-nord2-vbus-fast-edge.patch |
| 0710 | `0710-nord2-sim-pin-state.patch` | SIM, carrier, NR experiment, modem NV blob source | yes | jbts-20261115 | identical; file xnu/patches/0710-nord2-sim-pin-state.patch |
| 0711 | `0711-nord2-sim-generic-apn.patch` | SIM, carrier, NR experiment, modem NV blob source | yes | jbts-20261115 | identical; file xnu/patches/0711-nord2-sim-generic-apn.patch |
| 0712 | `0712-nord2-nr-reporting.patch` | SIM, carrier, NR experiment, modem NV blob source | yes | jbts-20261115 | identical; file xnu/patches/0712-nord2-nr-reporting.patch |
| 0713 | `0713-nord2-simcache-helper-sysctls.patch` | SIM, carrier, NR experiment, modem NV blob source | yes | jbts-20261115 | identical; file xnu/patches/0713-nord2-simcache-helper-sysctls.patch |
| 0714 | `0714-nord2-nr-ui-experiment.patch` | SIM, carrier, NR experiment, modem NV blob source | yes | jbts-20261115 | identical; file xnu/patches/0714-nord2-nr-ui-experiment.patch |
| 0715 | `0715-nord2-modem-nv-blob-rootfs-file.patch` | SIM, carrier, NR experiment, modem NV blob source | yes | jbts-20261115 | identical; file xnu/patches/0715-nord2-modem-nv-blob-rootfs-file.patch |
| 0720 | `0720-nord2-bt-controller.patch` | Bluetooth controller/transport/registry | yes | bt-20261115 |  |
| 0721 | `0721-nord2-bt-transport.patch` | Bluetooth controller/transport/registry | yes | bt-20261115 |  |
| 0722 | `0722-nord2-bt-registry.patch` | Bluetooth controller/transport/registry | yes | bt-20261115 |  |
| 0730 | `0730-nord2-sandbox-container-path-appex.patch` | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | jbts-20261115 | identical; file xnu/patches/0730-nord2-sandbox-container-path-appex.patch |
| 0731 | `0731-nord2-sandbox-fileprovider-log.patch` | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | jbts-20261115 | identical; file xnu/patches/0731-nord2-sandbox-fileprovider-log.patch |
| 0732 | `0732-nord2-memfloor-lsd.patch` | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | jbts-20261115 | identical; file xnu/patches/0732-nord2-memfloor-lsd.patch |
| 0733 | `0733-nord2-hfs-apfs-fsctl.patch` | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | jbts-20261115 | identical; file xnu/patches/0733-nord2-hfs-apfs-fsctl.patch |
| 0734 | `0734-nord2-root-apfs-typename.patch` | sandbox container op, sandbox log, memory floor, HFS-as-APFS | yes | jbts-20261115 | identical; file xnu/patches/0734-nord2-root-apfs-typename.patch |
| 0750 | `0750-nord2-gpu-mmu-fault-record-revive.patch` | GPU fault recovery | yes | jbts-20261115 | regenerated (rebased onto the stage) |
| 0760 | `0760-nord2-scp-sensors-ios.patch` | sensors (motion, orientation, light) | yes | sensors-20261115 |  |
| 0761 | `0761-nord2-scp-orientation-light.patch` | sensors (motion, orientation, light) | yes | sensors-20261115 |  |
| 0762 | `0762-nord2-scp-light-summary.patch` | sensors (motion, orientation, light) | yes | sensors-20261115 |  |
| 0790 | `0790-nord2-wifi-rssi-bounds.patch` | Wi-Fi auto-join RSSI bounds | yes | autojoin-20261115 |  |

## Withheld pieces (not in this tree)

| file | reason |
|---|---|
| `0084-nord2-gpu.patch` | tegu |
| `0085-sweet-fb-surface-retention.patch` | tegu |
| `0111-iokit-trap-fast-path.patch` | tegu |
| `0113-transport-trap-block-yields-to-frontend.patch` | tegu |
| `0188-kernel-stress-clients.patch` | tegu |
| `0189-stress-bootarg-n2st.patch` | tegu |
| `0260-nord2-modem-fs-server.patch` | embedded modem NVRAM blob |
| `0261-nord2-modem-device-maps-early-rings.patch` | embedded modem NVRAM blob |
| `0262-nord2-modem-nvdisk-fallback.patch` | embedded modem NVRAM blob |
| `0263-nord2-modem-at-client.patch` | embedded modem NVRAM blob |
| `0265-nord2-modem-full-start3s.patch` | embedded modem NVRAM blob |
| `0266-nord2-modem-full-start3s-nopause.patch` | embedded modem NVRAM blob |
| `0267-nord2-modem-full-start10s.patch` | embedded modem NVRAM blob |
| `0268-nord2-modem-one-ring-mapping.patch` | embedded modem NVRAM blob |
| `0269-nord2-modem-ccb-ctrl.patch` | embedded modem NVRAM blob |
| `0270-nord2-modem-at-registration-poll.patch` | embedded modem NVRAM blob |
| `0271-nord2-modem-at-data.patch` | embedded modem NVRAM blob |
| `0272-nord2-modem-persistent-p1-test.patch` | embedded modem NVRAM blob |
| `0273-nord2-modem-daily.patch` | embedded modem NVRAM blob |
| `0274-nord2-modem-dpmaif-datapath.patch` | embedded modem NVRAM blob |
| `0275-nord2-modem-p2t.patch` | embedded modem NVRAM blob |
| `0276-nord2-modem-p2f.patch` | embedded modem NVRAM blob |
