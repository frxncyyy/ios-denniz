# What the vendor codec objects do with /dev/vcu, /dev/vpu and mmap

Owner of this file: D01 (`hw-d01`). Consumer: V40 (`/dev/vcu` device model) and IV.
Method: `python3 gpu/hwcodec/facts/vcu_usage.py` (deterministic, reads the five objects from the extracted
`/vendor` copy, prints every `ioctl`/`mmap`/`mmap64`/`open`/`__open_2` call site with the literal argument
built in the same basic block). The `_IOC` decoding below uses `_IOC(dir,type,nr,size)` with `_IOW` = 1,
`_IOWR` = 3, and the **32-bit AAPCS struct sizes** (u64 members 8-byte aligned) of
`nord2-current-kernel/include/uapi/linux/mtk_vcu_controls.h`.

## 1. Which device nodes are opened

| object | function | call site | path | mode |
|---|---|---|---|---|
| libvpud_vcodec.so | `vdec_codec_msg_handler` | 0xc528 | `/dev/vcu` | O_RDWR (r1=2) |
| libvpud_vcodec.so | `vdec_codec_msg_handler` | 0xc540 | `/dev/vpu` | O_RDWR (r1=2) |
| libvpud_vcodec.so | `venc_vcodec_msg_handler` | 0x11bd6 | `/dev/vcu` | O_RDWR (r1=2) |
| libvpud_vcodec.so | `venc_vcodec_msg_handler` | 0x11bf6 | `/dev/vcu` | O_RDWR (r1=2) |
| libvcodec_utility.so | `mt_sched_*affinity` | 0x3423c/0x342ba/0x3432e | `/dev/mtk_sched` | O_RDWR |
| libvcodec_utility.so | `eHalMCDICtrl` | 0x19656 | `/proc/mcdi/sodi_en` | O_RDONLY |
| libvcodec_utility.so | `eHalDVFSDCMCtrl` | 0x19712 | `/sys/power/cpufreq_state` | O_RDONLY |
| libvcodec_utility.so | `eVideoBWLimit` | 0x19be4 | `/sys/bus/platform/drivers/mem_bw_ctrl/concurrency_scenario` | O_RDONLY |
| libvcodecdrv.so | `QueryCapability` / `eVEncDrvQueryCapability` | 0x7626e/0x778fa/0x77f9c | `/proc/device-tree/chosen/atag,devinfo` | O_RDONLY (r1=0) |

[V] **`libvcodecdrv.so` makes no ioctl and no mmap call at all** (3 open sites, all the devinfo file above).
All kernel access of the codec stack goes through the OAL in `libvcodec_utility.so` (`eVideo*`), so the
encoder register window is mapped by the same `eVideoMMAP`/`vdec_drv_init_hw_info` code, not by a separate
path in the codec library. This closes open question 8.8 of FACTS.md.

[V] `/dev/vpu` **is** opened, but only by the decoder path of `libvpud_vcodec.so` (and the string exists in
`bin/vpud` too); the encoder path opens `/dev/vcu` only. In this kernel `/dev/vpu` has no driver
(`CONFIG_VIDEO_MEDIATEK_VPU is not set`, PLAN §1.1), so the open fails with ENOENT on the phone and the code
must have a `/dev/vcu` fallback; V40 needs a `/dev/vcu` node and may ignore `/dev/vpu` (or provide it as an
alias). Evidence: `vcu_usage.py` output lines above; `strings` on the objects shows exactly `/dev/vcu`,
`/dev/vpu`, `/dev/mtk_sched`.

## 2. ioctl requests with a literal at the call site

| object | function | call site | request | decoded | struct size |
|---|---|---|---|---|---|
| libvpud_vcodec.so | `venc_vcodec_msg_handler` | 0x13cb4 | `0x40587600` | **VCU_SET_OBJECT** `_IOW('v',0,share_obj)` | 0x58 |
| libvcodec_utility.so | `eVideoMemAlloc` | 0x17f3e | `0xc0207606` | **VCU_PA_ALLOCATION** `_IOWR('v',6,mem_obj)` | 0x20 |
| libvcodec_utility.so | `eVideoMemFree` | 0x18444 | `0xc0207607` | **VCU_PA_FREE** `_IOWR('v',7,mem_obj)` | 0x20 |
| libvcodec_utility.so | `eVideoSetGCECmdFlush` | 0x18ba0 | `0x40207608` | **VCU_GCE_SET_CMD_FLUSH** `_IOW('v',8,gce_cmdq_obj)` | 0x20 |
| libvcodec_utility.so | `eVideoWaitGCECallBack` | 0x18cac | `0x40107609` | **VCU_GCE_WAIT_CALLBACK** `_IOW('v',9,gce_obj)` | 0x10 |

[V] The numbers agree with the header once the struct sizes are the 32-bit ones: `share_obj` = 0x58 (observed
directly in the SET_OBJECT request), `mem_obj` = 0x20 (u64 iova, u32 len, 4 pad, u64 pa, u64 va), `gce_cmdq_obj`
= 0x20 (two u64 + four u32), `gce_obj` = 0x10 (u64 + two u32). The header's `COMPAT_VCU_*` macros use
`struct compat_mem_obj`, but the vendor code passes `struct mem_obj` (same 0x20 layout in 32-bit), so both
decode identically; V40 should accept the numbers above.

[V] `VCU_GET_OBJECT` (expected `0xc058760a`, `_IOWR('v',10,share_obj)`) is **not** a literal at any call site:
two ioctl calls pass r1 from memory/a parameter - `md32_ipi_send` (0x14544, `ldrd r0,r1,[r1]` from the ipi
object) and `send_obj` (0x1459a, `mov r1, r5` = third argument). So the receive path builds the request from
the shared object. V40 must implement GET_OBJECT with the same 0x58 payload even though no literal shows it.

[V] Not VCU: `0x40044d34` and `0x40044d35` (magic `'M'`, nr 0x34/0x35, size 4) in `eHalCmdProc`/`eVideoIncLogCount`
- MediaTek ION custom ioctls, not /dev/vcu; and `0x40106600` / `0x80106602` / `0x40046601` (magic `'f'`,
nr 0/2/1) on `/dev/mtk_sched` - the MediaTek scheduler node, not /dev/vcu.

## 3. mmap

| object | function | call site | length | prot | flags | offset |
|---|---|---|---|---|---|---|
| libvcodec_utility.so | `vdec_drv_init_hw_info` | 0x1e1ea | **0x40000** | 3 (RW) | 1 (SHARED) | **0x16000000** |
| libvcodec_utility.so | `eVideoMemMAP` | 0x1822a | computed (page-aligned length) | 3 | 1 | computed (r5:r8 64-bit) |
| libvcodec_utility.so | `eVideoMMAP` | 0x19a40 | from the caller's struct | 3 | 1 | from the caller's struct |

[V] The register window is mapped with `mmap(NULL, 0x40000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x16000000)`
- offset = the VDEC register base, length = 0x40000. `mtk_vcu_mmap()` (mtk_vcu.c:1653) takes `pa_start =
vma->vm_pgoff << PAGE_SHIFT`, and for `pa_start < MAP_PA_BASE_1GB` (0x40000000) calls `vcu_check_reg_base()`
(mtk_vcu.c:679), which accepts only addresses inside the four `map_base[]` windows and only for `vcuid == 0`.
So V40's mmap must accept the physical base as the offset and return the corresponding register window; the
other two mmaps use the shared-memory bases (`MAP_SHMEM_ALLOC_BASE 0x80000000`, `MAP_SHMEM_MM_BASE`,
`MAP_SHMEM_PA_BASE 0x800000000`, mtk_vcu.c:100-132) or the queue's own dmem.
[I] The two shmem mmaps are the daemon's shared buffers (the AP side writes into them); V40 can map them to
plain guest memory as its deliverable says, because nothing in the vendor code reads them as registers.

## 4. What this means for V40

1. Provide `/dev/vcu` (and an alias `/dev/vpu` is harmless): `open` with O_RDWR must succeed for the daemon.
2. Implement at least SET_OBJECT, GET_OBJECT, PA_ALLOCATION, PA_FREE, GCE_SET_CMD_FLUSH, GCE_WAIT_CALLBACK,
   MVA_ALLOCATION/FREE, CACHE_*, SET_MMAP_TYPE with the exact 32-bit request numbers above (the daemon sends
   the literal numbers, so a wrong `_IOC` size in the model returns EINVAL from the kernel ABI check).
3. mmap: offset 0x16000000 + len 0x40000 must give the VDEC register window; the encoder maps its own base the
   same way (0x17020000, not seen in these two objects because the encoder window map is in `libvcodecdrv.so`,
   which was not in the scan set - if V40 needs it, run `vcu_usage.py --object libvcodecdrv.so` after adding it
   to OBJECTS).
4. `/dev/mtk_sched` and the ION ioctls are out of scope for V40 but must exist as shims: V33's n2vfs can answer
   them with ENOSYS if the code tolerates it (it checks the open() result).
5. `/proc/device-tree/chosen/atag,devinfo` must return a devinfo string: libvcodecdrv.so opens it with
   O_RDONLY in `QueryCapability`/`eVEncDrvQueryCapability` (0x7626e, 0x778fa, 0x77f9c).

Not done / open: the two computed mmap offsets and the `md32_ipi_send`/`send_obj` request values are runtime
values, not literals, and are reported as such.
