#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare differential-harness output files and print the per-configuration table.

Input lines are "kernel seed result" with hexadecimal seed and result, as written by
tests/emu/diffrun.c and by the host reference main generated in run_diff.sh.
"""
import argparse
import os
import sys

# Which instruction-class package is most likely responsible when a kernel fails.  The goal is
# to route failures quickly, not to be exact: several kernels span more than one class.
OWNERS = {
    'k_addsub': 'V10', 'k_logic': 'V10', 'k_mul': 'V10,V11', 'k_mul64': 'V10,V11',
    'k_madd64': 'V10,V11', 'k_shift_imm': 'V10', 'k_shift_var': 'V10', 'k_rotate': 'V10',
    'k_rev': 'V11', 'k_bitfield': 'V11', 'k_select': 'V10', 'k_clz': 'V10',
    'k_switch_dense': 'V10,V12', 'k_switch_sparse': 'V10,V12', 'k_recursion': 'V10,V12',
    'k_saturate': 'V11', 'k_absdiff': 'V10', 'k_signext': 'V11',
    'k_memcpy_loop': 'V12,V16', 'k_memmove_loop': 'V12,V16', 'k_memset_loop': 'V12,V16',
    'k_call_memcpy': 'V12,V16 + support_arm', 'k_call_memmove': 'V12,V16 + support_arm',
    'k_call_memset': 'V12,V16 + support_arm', 'k_call_strlen': 'V12,V16 + support_arm',
    'k_strlen_loop': 'V12,V16', 'k_strcmp_loop': 'V12,V16', 'k_array_sum': 'V12,V16',
    'k_array_reverse': 'V12,V16', 'k_struct_copy': 'V12,V16', 'k_byte_load': 'V12,V16',
    'k_stack_spill': 'V12,V16',
    'k_add64': 'V10,V11', 'k_mul64p': 'V10,V11', 'k_udiv32': 'V11', 'k_sdiv32': 'V11',
    'k_udiv64': 'host shim + V10,V11', 'k_sdiv64': 'host shim + V10,V11',
    'k_mod64': 'host shim + V10,V11', 'k_udiv64_manual': 'V10,V11',
    'k_shift64': 'V10,V11', 'k_cmp64': 'V10', 'k_bswap64': 'V10,V11',
    'k_faddsub': 'V17', 'k_fmuldiv': 'V17', 'k_fconv': 'V17', 'k_daddsub': 'V17',
    'k_dmuldiv': 'V17', 'k_dconv': 'V17', 'k_fcompare': 'V17', 'k_fround': 'V17',
    'k_fvec_loop': 'V17,V18,V19', 'k_dvec_loop': 'V17,V18,V19', 'k_fminmax': 'V17',
    'k_i8_loop': 'V18,V19,V20', 'k_i16_loop': 'V18,V19,V20', 'k_u8_sad': 'V18,V19',
    'k_i8_pack': 'V18,V19', 'k_i16_mul': 'V18,V19', 'k_u8_memcpy': 'V20',
    'k_crc32': 'V14,V10', 'k_hash_bytes': 'V12,V16', 'k_matrix': 'V10,V12',
    'k_branch': 'V10,V12', 'k_parity': 'V10', 'k_bitreverse': 'V10,V11',
    'k_countbits': 'V10', 'k_bswap16': 'V11',
    'k_v11_extend': 'V11', 'k_v11_bitfield': 'V11', 'k_v11_reverse': 'V11',
    'k_v11_sat': 'V11', 'k_v11_qadd': 'V11', 'k_v11_pack': 'V11',
    'k_v11_mul16': 'V11', 'k_v11_dual': 'V11', 'k_v11_usad': 'V11',
    'k_v11_div': 'V11,V10',
    'k14_alu': 'V14', 'k14_mem': 'V14,V12', 'k14_branch': 'V14',
    'k12_ldst': 'V12,V18', 'k12_ldrd': 'V12,V18', 'k12_signed': 'V12',
    'k12_ldm_stm': 'V12', 'k12_branch': 'V12', 'k12_stack': 'V12',
    'k_v10_alu_imm': 'V10', 'k_v10_alu_reg': 'V10', 'k_v10_shift_imm': 'V10',
    'k_v10_shift_reg': 'V10', 'k_v10_cmp_branch': 'V10,V12', 'k_v10_mul32': 'V10',
    'k_v10_mul64': 'V10,V11', 'k_v10_movw': 'V10', 'k_v10_clz': 'V10',
    'k_v10_adr_switch': 'V10,V12',
    'k_v15_alu': 'V15', 'k_v15_adcsbc': 'V15,V14', 'k_v15_muldiv': 'V15,V11',
    'k_v15_mul64': 'V15', 'k_v15_bitfield': 'V15', 'k_v15_sat': 'V15',
    'k_v15_extend': 'V15', 'k_v15_rev': 'V15,V14',
    'k_neon_vshl': 'V19,V18', 'k_neon_vneg': 'V18,V20', 'k_neon_vmla16': 'V18,V19',
    'k_neon_vminmax': 'V18', 'k_neon_vshrn': 'V19,V20', 'k_neon_vmovn': 'V19,V20',
    'k_neon_vldst': 'V20',
    'k_neon_vmul': 'V18', 'k_neon_vmull': 'V19', 'k_neon_vmla': 'V18',
    'k_neon_vsubl': 'V19', 'k_neon_vcgtbit': 'V18,V20',
    'k18_addsub8': 'V18', 'k18_addsub16': 'V18', 'k18_addsub32': 'V18',
    'k18_minmax8': 'V18', 'k18_minmax32': 'V18', 'k18_absdiff': 'V18',
    'k18_mul16': 'V18', 'k18_logic': 'V18', 'k18_select': 'V18',
    'k18_shift_var': 'V19', 'k18_halving': 'V18', 'k18_cmpeq': 'V18',
    'k18_float': 'V18,V17',
    'k_v16_ldst_sizes': 'V16', 'k_v16_ldst_imm': 'V16', 'k_v16_ldrd': 'V16',
    'k_v16_ldm': 'V16', 'k_v16_switch_dense': 'V16', 'k_v16_switch_wide': 'V16',
    'k_v16_branch': 'V16', 'k_v16_stack': 'V16', 'k_v16_atomic': 'V16,V12',
}


def read_results(path):
    out = {}
    if not os.path.exists(path):
        return None
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            out[(parts[0], parts[1].lower())] = parts[2].lower()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', required=True)
    ap.add_argument('--ref-o0', required=True)
    ap.add_argument('--results-dir', required=True)
    ap.add_argument('--configs', required=True, help='space separated configuration names')
    ap.add_argument('--kernels', required=True, help='file with one kernel name per line')
    ap.add_argument('--seeds', required=True, help='comma separated hexadecimal seeds')
    args = ap.parse_args()

    kernels = [l.strip() for l in open(args.kernels, encoding='utf-8')
               if l.strip() and not l.strip().startswith('#')]
    seeds = [s.strip().lower().replace('0x', '').zfill(8) for s in args.seeds.split(',')]
    seeds = [s[-8:] for s in seeds]
    configs = args.configs.split()

    ref = read_results(args.ref)
    ref0 = read_results(args.ref_o0)
    if ref is None or ref0 is None:
        print('N2TEST T01 FAIL missing host reference output', file=sys.stderr)
        return 1

    # Kernel/C consistency: two host optimisation levels must agree, otherwise the kernel
    # itself is undefined and cannot judge the emulator.
    unstable = []
    for k in kernels:
        for s in seeds:
            a, b = ref.get((k, s)), ref0.get((k, s))
            if a is None or b is None or a != b:
                unstable.append((k, s, a, b))
    if unstable:
        print('reference -O2 and -O0 disagree (kernel C is undefined):')
        for k, s, a, b in unstable[:20]:
            print('  %-20s seed=%s O2=%s O0=%s' % (k, s, a, b))
        print('N2TEST T01 FAIL %d unstable kernel/seed pairs' % len(unstable))
        return 1

    print('%-12s %8s %8s %8s %8s %8s' % ('configuration', 'calls', 'pass', 'mismatch', 'stop', 'missing'))
    total = passed = all_bad = 0
    failures = {}
    for cfg in configs:
        res = read_results(os.path.join(args.results_dir, cfg + '.txt'))
        n = ok = bad = stopped = missing = 0
        for k in kernels:
            for s in seeds:
                n += 1
                total += 1
                exp = ref.get((k, s))
                if res is None:
                    missing += 1
                    continue
                got = res.get((k, s))
                if got is None:
                    missing += 1
                elif got.startswith('stop:'):
                    stopped += 1
                    failures.setdefault(k, []).append((cfg, s, exp, got))
                elif exp is None:
                    missing += 1
                elif got == exp:
                    ok += 1
                    passed += 1
                else:
                    bad += 1
                    failures.setdefault(k, []).append((cfg, s, exp, got))
        all_bad += bad + stopped + missing
        print('%-12s %8d %8d %8d %8d %8d' % (cfg, n, ok, bad, stopped, missing))

    if failures:
        print('\nfailures by kernel (first three each):')
        for k in sorted(failures):
            print('  %-20s owner=%-22s %d failing calls' % (k, OWNERS.get(k, '?'), len(failures[k])))
            for cfg, s, exp, got in failures[k][:3]:
                print('      %-10s seed=%s expected=%s got=%s' % (cfg, s, exp, got))
    if all_bad or passed != total:
        print('\nN2TEST T01 FAIL %d/%d calls differ or are missing' % (all_bad, total))
        return 1

    print('\nN2TEST T01 PASS %d/%d' % (passed, total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
