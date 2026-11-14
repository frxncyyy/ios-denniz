# V14 — T32 16-bit instructions: status

Worker: hw-v14. Owned files: `gpu/hwcodec/emu/n2a32_t16.c`, `gpu/hwcodec/emu/test_v14/**`.
Interface: `n2a32_t16(struct n2a32_cpu *, uint32_t insn)` from `include/n2a32.h` (frozen).
Host-tested only. **Not run on the phone** (no phone access from this worker; there is
nothing phone-specific in this package anyway).

Last update: 11 November 2026.

## Implemented

`n2a32_t16()` implements every 16-bit T32 encoding except the deliberate list below.
Contract: the core has already evaluated the IT condition, set `c->r[15] = address + 4`,
`c->next_pc`, `c->in_it`; the class itself branches only through `n2a32_branch()` /
`n2a32_bx()` and accesses memory only through `n2a32_ld8/16/32`, `n2a32_st8/16/32`.
It returns `N2A32_OK` or `N2A32_UNDEF`; it never prints, aborts, or touches the host.

| Encoding group | Mnemonics / forms |
|---|---|
| Shift immediate, add/sub (000xx) | LSL, LSR, ASR (immediate); ADD/SUB (register); ADD/SUB (imm3) |
| Move/compare/add/sub imm8 (001xx) | MOV, CMP, ADD, SUB (immediate 8) |
| Data processing (010000) | AND, EOR, LSL, LSR, ASR, ADC, SBC, ROR, TST, RSB, CMP, CMN, ORR, MUL, BIC, MVN |
| High registers / exchange (010001) | ADD (high; incl. ADD Rd,SP,Rd and ADD SP,Rm); CMP (high); MOV (high); BX; BLX (register) |
| LDR literal (01001) | LDR Rt, [PC, #imm8*4], Align(PC,4) |
| Load/store register offset (0101) | STR, STRH, STRB, LDRSB, LDR, LDRH, LDRB, LDRSH |
| Load/store immediate (011xxx, 1000xx) | STR/LDR word, STRB/LDRB, STRH/LDRH |
| SP-relative (1001x) | STR/LDR [SP, #imm8*4] |
| ADR / ADD SP (1010x) | ADR; ADD Rd, SP, #imm8*4; ADD/SUB SP, #imm7*4 |
| Extend (1011 0010) | SXTH, SXTB, UXTH, UXTB |
| Compare/branch on zero | CBZ, CBNZ (forward, imm5:0) |
| Push/pop | PUSH {r0-r7[,lr]}; POP {r0-r7[,pc]} (PC through n2a32_bx) |
| Reverse bytes | REV, REV16, REVSH |
| Breakpoint / IT / hints / exceptions | BKPT; IT (writes c->itstate); NOP, YIELD, WFE, WFI, SEV, SEVL and reserved hints as NOPs; SVC; UDF |
| Branches | B<c> (own condition via n2a32_cond), B (unconditional) |
| Load/store multiple | STMIA, LDMIA of r0-r7 (the deliverable text omitted this group; V16 owns only 32-bit forms, so V14 implements it) |

Flag rules implemented exactly as the Arm ARM (verified against the A32/T32 instruction
reference pages, Nov 2026):
- `setflags = !c->in_it` for LSL/LSR/ASR (immediate and register), AND/EOR/ORR/BIC/MVN,
  ADC/SBC/RSB, MUL, MOV/CMP/ADD/SUB imm8, and the high-register ADD/MOV as applicable;
- **CMP, CMN and TST always update flags**, including inside an IT block
  ("16-bit instructions in the IT block, other than CMP, CMN and TST, do not set flags");
- the high-register **ADD and MOV never set flags** (their decode has `setflags = FALSE`);
  high-register CMP always sets N,Z,C,V;
- shift special cases: LSL #0 and ROR #0 preserve C; LSR/ASR #0 (and register amounts 0)
  mean a shift of 32; register amounts use Rs<7:0> with LSL/LSR/ASR saturating at 32 and
  ROR taken modulo 32 (amount 32 = unchanged value, C = bit 31);
- ADR/LDR-literal use `Align(c->r[15], 4)`; BLX sets `LR = (c->r[15] - 2) & ~1 | 1`;
- POP {pc}, BX and BLX use `n2a32_bx` (BXWritePC); ALU writes to PC
  (`MOV pc, Rm`, `ADD pc, Rm`) use `n2a32_branch`.

Architecturally UNPREDICTABLE encodings are executed deterministically with their natural
semantics (they cannot crash and are never emitted by compiled user code): CMP with PC,
BX/BLX with PC, ADD/MOV with PC in a non-last IT slot, IT inside IT, LSL #0 inside IT
(executed as MOV, one of the architecturally permitted behaviours).

## Deliberately N2A32_UNDEF

| Encoding | Why |
|---|---|
| SETEND (0xB650/0xB651) | privileged-checked endianness switch; this interpreter is a little-endian EL0 model and does not model PSTATE.E |
| CPS (0xB660-0xB66F) | privileged (EL0 execution is UNDEFINED) |
| HLT (0xBA80-0xBABF) | debug/semihosting instruction, not part of the user-mode set used by the vendor libraries |
| Reserved holes 0xB300-0xB3FF, 0xB600-0xB6FF (except the two above), 0xB700-0xB8FF, 0xBB00-0xBBFF | unallocated encodings |
| IT with firstcond=1111, or firstcond=1110 and popcount(mask)!=1 | architecturally UNPREDICTABLE; UNDEFINED is one of the permitted behaviours |
| top five bits 11101/11110/11111, and any `insn > 0xFFFF` | 32-bit prefixes / not a 16-bit unit; defensive (the core routes these elsewhere) |
| UDF, BKPT, SVC | by the shared class convention these set `c->stop` (`STOP_UNDEF` / `STOP_BKPT` / `STOP_SVC`), `c->stop_insn`, `c->stop_addr = c->r[15]-4` and return `N2A32_UNDEF`; they are implemented, not stubs |

No other encoding is returned as UNDEF: the exhaustive sweep proves the function returns
OK or UNDEF (never anything else) for all 65536 16-bit values.

## Tests

Command (builds only under `/tmp/n2hw-V14`, ~4 s, two builds):

    bash gpu/hwcodec/emu/test_v14/run_tests.sh

Last lines:

    V14 encodings: 269 instructions, 225 named, 244 cross-checked against --show-encoding
    N2TEST V14 PASS 453/453

How it works:
- `t16_enc.s` holds 269 instructions written as assembly, 225 of them annotated
  `@ E_name`. `run_tests.sh` assembles it with
  `llvm-mc -triple=thumbv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8`,
  disassembles the object with `llvm-objdump`, and `gen_enc.py` asserts every instruction
  is exactly two bytes and emits the `E_name` macros. It also runs
  `llvm-mc ... --show-encoding` and cross-checks the 244 non-branch encodings against the
  object bytes (the 25 branch encodings carry assembler fixups in the printout, so their
  bytes come from the object). No encoding is typed by hand.
- `t16_tests.c` calls `n2a32_t16()` directly on a `struct n2a32_cpu` whose
  `mem->base` is a 16 MiB `calloc` (guest addresses below 16 MiB). Expected registers,
  flags, memory and next PC are literals worked out from the Arm ARM pseudocode.
- 453 cases: every mnemonic has at least 6 cases (many more for the 16 data-processing
  ops), including carry/borrow/overflow/zero/negative edges, in-IT flag suppression,
  CMP/CMN/TST flag updates inside IT, SP/PC operand forms, every load/store size and
  addressing variant, every branch condition taken and not taken, BLX/POP-PC
  interworking, IT mask encodings, and the stop codes of BKPT/SVC/UDF.
- Decode census: all 65536 16-bit values are executed with a clean CPU; the test asserts
  exactly 57030 OK and 8506 UNDEF, and that the UNDEF set is only the documented one
  (reserved holes, SETEND/CPS/HLT, BKPT/UDF/SVC stop codes, 32-bit prefixes, BX/BLX with
  bits 2:0 != 000, and the UNPREDICTABLE IT masks).
- Sweep: all 65536 16-bit values and 200000 deterministic random words, random
  registers/flags/IT state, with every megabyte marked MMIO so a random load/store cannot
  escape the host allocation. Runs under ASan+UBSan and once more at `-O2` without
  sanitizers, exactly the flags in WORKER_RULES §4.
- `k14_t16.c`: a seed-pure C kernel for T01's differential harness (k14_alu,
  k14_mem, k14_branch), posted to hw-t01; it builds for x86-64 and for
  `thumbv7a-none-eabi -Os` (checked with clang 23.1.4) and emits a broad mix of 16-bit
  encodings.

Tool versions: clang/LLVM 23.1.4, Linux x86-64.

Cross-compile check (compiled for iOS, not run there):

    clang --target=arm64-apple-ios14.0 -std=c11 -O2 -Wall -Wextra -Werror \
        -nostdlibinc -isystem gpu/video/sdk/sysinc -I gpu/hwcodec/include \
        -c gpu/hwcodec/emu/n2a32_t16.c -o /tmp/n2hw-V14/n2a32_t16_ios.o
    -> Mach-O 64-bit arm64 object

The host tests above are the only execution evidence; nothing has run on the phone.

## Interfaces / coordination

- ROUTING posted to hw-v02: every 16-bit fetch that is not a 32-bit prefix goes to
  `n2a32_t16`; a single `size == 2 -> n2a32_t16` rule is sufficient.
- STOP convention (SVC/BKPT/UDF): same as V12's task text; posted to hw-v02 with the
  request that the core preserve a non-NONE `c->stop` when a class returns UNDEF.
- IT contract: V14 writes `c->itstate = firstcond:mask`; the core must ITAdvance after
  every following T32 instruction but **not after the IT itself** (Arm ARM:
  `ShouldAdvanceIT = FALSE` in the IT operation). Posted to hw-v02.
- Kernel for T01: `emu/test_v14/k14_t16.c`; T01 owns `tests/emu`, so it must be wired
  there by hw-t01 (or copied with my agreement).

## Unresolved / open

1. **Census acceptance not yet checked**: `FACTS.md`/`facts/census.py` (hw-d01) was not
   ready when this file was written. My only non-implemented T16 encodings are SETEND,
   CPS and HLT; I asked hw-d01 to flag them (or any reserved hole) if the vendor libraries
   use them. If the census finds one, implementing it is a small extension.
2. **Core stop convention**: waiting for hw-v02 to confirm that a class-set `c->stop`
   survives the UNDEF return. The package follows the V12 task convention, which is the
   only written one. If V02 chooses a different mechanism, only the SVC/BKPT/UDF block
   needs changing.
3. **BKPT inside IT**: architecturally BKPT always executes, regardless of the IT
   condition. If the core pre-skips instructions whose IT condition fails, a BKPT in an
   IT block would be skipped; that is a core-side decision, flagged to hw-v02, not
   something the class can detect.
4. **Differential harness**: kernel written; not yet run because `emu/n2a32_core.c` (V02)
   was not in the tree at the time of writing.
5. **SETEND** could in principle be implemented as a no-op for LE / switch the memory
   model for BE; deliberately left UNDEF rather than silently wrong, and listed above.
