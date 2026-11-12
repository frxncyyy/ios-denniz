# 30 — power each block and read the VENC registers back

**Goal:** exercise K02's `RWVC_POWER` path once per block and perform the first
real VENC register read-back. This is the first step that touches SPM,
infracfg, topckgen, the vencsys/vdec gates, larb7/8/4/5 and the MMIO windows.

## Preconditions

- `20-vcodec-readonly.md` passed.
- The display is on (the VEN parent is DIS; K02's power path refuses if the
  DIS status bit is clear).
- No `vcodecd` and no JPEG job is running.

## Steps

On the phone, as root:

```sh
/usr/local/bin/rwvc_probe all      # info + each block on/read one word/off
/usr/local/bin/rwvc_probe read     # VENC on, six reads, VENC off
```

Then leave the phone alone for 60 s and check it still answers:

```sh
cd gpu/build/autotest
python3 phone.py status
python3 phone.py logs /tmp/n2hw-T02/phone/power-after.log
```

The camera bring-up once died 27-44 s after releasing a domain's bus
protection while its bus parent was off; this wait is deliberate.

## Expected

`rwvc_probe all`:

```text
N2VC info abi=1 powered=0x0 ...
N2VC power VENC on -> 0
N2VC regs VENC done=1/1
N2VC read VENC 0x0000 0x........
N2VC power VENC off -> 0
N2VC power VENC_C1 on -> 0
N2VC regs VENC_C1 done=1/1
N2VC read VENC_C1 0x0000 0x........
N2VC power VENC_C1 off -> 0
N2VC power VDEC_SOC on -> 0
N2VC regs VDEC_SOC_GCON done=1/1
N2VC read VDEC_SOC_GCON 0x0000 0x........
N2VC power VDEC_SOC off -> 0
N2VC power VDEC_CORE on -> 0
N2VC regs VDEC_SYS done=1/1
N2VC read VDEC_SYS 0x0000 0x........
N2VC power VDEC_CORE off -> 0
N2VC all PASS
N2VC verdict PASS
```

`rwvc_probe read`:

```text
N2VC info abi=1 powered=0x0 ...
N2VC power VENC on -> 0
N2VC regs VENC done=6/6
N2VC read VENC 0x0000 0x........
N2VC read VENC 0x0004 0x........
N2VC read VENC 0x005c 0x........
N2VC read VENC 0x0060 0x........
N2VC read VENC 0x0100 0x........
N2VC read VENC 0x0104 0x........
N2VC power VENC off -> 0
N2VC verdict PASS
```

The register values are the first hardware evidence: record all six. They are
not expected to have particular values on the first boot (the bootloader may
leave anything); what matters is that the reads succeed and that 0x5c/0x60
(the VENC interrupt status/ack words K02 documents) are readable. Do not write
any value yet.

## Pass criteria

- `N2VC all PASS` and `N2VC verdict PASS`.
- `N2VC regs VENC done=6/6`.
- No `NORD2 VCODEC:` error line after the commands. K02 prints on mux/gate/
  protection/power failures only; silence is success.
- The phone still answers `phone.py status` after 60 s.

## If it fails

| Line | Meaning |
|---|---|
| `N2VC power ... -> -EACCES` | display domain off, or the minor is wrong (must be minor 0/root) |
| `N2VC power ... -> -ETIMEDOUT` | SPM status or SRAM ack timeout; the request was withdrawn |
| `N2VC power ... -> -EIO` | mux or gate read-back mismatch; `NORD2 VCODEC:` names the register |
| `N2VC regs ... -> -EACCES` | window's block is not powered (the probe powers it first) |
| `N2VC read ... EACCES` | same, per op |
| phone dies within 60 s | the VEN/VDEC power sequence damaged a shared parent; recover with `50-recovery.md` and report the last `NORD2 VCODEC:` line |

Do not run the JPEG device test or `hwc.py` command 7 concurrently: the VEN
domain reference is shared and K02's model is not a scheduler.
