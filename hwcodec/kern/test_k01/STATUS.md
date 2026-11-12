# K01 — host tests of /dev/redwood-jpeg

Full package status, phone test plan and the not-done list: `../../dsh-state/workers/K01/STATUS.md`.

    $ bash gpu/hwcodec/kern/test_k01/run_tests.sh
    N2TEST K01 PASS 822/822 (violations 0)      # ASan+UBSan
    N2TEST K01 PASS 822/822 (violations 0)      # -O2, no sanitizers
    N2TEST K01 PASS 822/822 (violations 0)      # TSan (one-job lock, two callers)

The test models the MT6893 VEN domain, vencsys gates, larb7 and the JPEG encoder from
`gpu/hwcodec/kernel/b/bsd/dev/nord2_hwcodec.inc` and the phone read-backs in
`gpu/hwcodec/runs/q10hw1`, and fails on any register access with the domain off/protected or a
clock gated, wrong geometry/control/quality, DMA outside the 40 MiB claim, a display-SMI write,
or an injected copyin/copyout fault. It covers every validation branch of
`gpu/hwcodec/include/redwood_jpeg.h`, the 4096x3072 maximum, NV12/NV21/YUYV/YVYU padding,
quality/restart/EXIF, power and bus faults, timeout/stall/size, the idle power-off thread call,
and the hw-k02 VEN hook. Host-tested only; never run on the phone.
