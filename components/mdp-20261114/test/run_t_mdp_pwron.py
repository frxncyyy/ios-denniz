#!/usr/bin/env python3
"""0673 host tests: t_mdp_pwron.c on the real nord2_mdp.inc (must pass) and on mutants (each must FAIL).
usage: python3 run_t_mdp_pwron.py [INC]   (default: ../piece-0673/patched-sources-0673/bsd/dev/nord2_mdp.inc)"""
import os, subprocess, sys, tempfile

H = os.path.dirname(os.path.abspath(__file__))
M = os.path.dirname(H)
INC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(M, 'piece-0673/patched-sources-0673/bsd/dev/nord2_mdp.inc')
CFLAGS = ['-std=c11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-Wall', '-Wextra',
          '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-misleading-indentation']

MUTANTS = [
    ('exclusive filter removed (child bits released)',
     'rel[k] = (en[r] | sta[r]) & n2mdp_psteps[k].mask & n2mdp_prot_exclusive(r);',
     'rel[k] = (en[r] | sta[r]) & n2mdp_psteps[k].mask & ~n2mdp_dis_bits[r];'),
    ('release order reversed (step1 before step2)',
     'static const struct n2mdp_pstep n2mdp_psteps[N2MDP_NPSTEPS] = {\n\t{ "step2:0", 0, 0x00800000u }, { "step2:1", 1, 0xa08802a8u }, { "step2:2", 2, 0x00000a82u }, { "step2:3", 3, 0x00100000u },\n\t{ "step1:0", 0, 0x00000400u }, { "step1:1", 1, 0x50440154u }, { "step1:2", 2, 0x00000155u },',
     'static const struct n2mdp_pstep n2mdp_psteps[N2MDP_NPSTEPS] = {\n\t{ "step1:0", 0, 0x00000400u }, { "step1:1", 1, 0x50440154u }, { "step1:2", 2, 0x00000155u },\n\t{ "step2:0", 0, 0x00800000u }, { "step2:1", 1, 0xa08802a8u }, { "step2:2", 2, 0x00000a82u }, { "step2:3", 3, 0x00100000u },'),
    ('restore after ack timeout removed', 'return n2mdp_prot_restore() ? 6 : 7;', 'return 6;'),
    ('owned rule ignores protection', 'return v & n2mdp_prot_mdp(r) & ~n2mdp_prot_child_off(r, pwr);', 'return 0u & v & pwr & r;'),
    ('no ack poll after CLR', 'int us = n2mdp_prot_poll(R->sta1, b, 0), us2', 'int us = 0, us2'),
    ('restore order = release order', 'n2mdp_pdown_order[N2MDP_NPSTEPS] = { 4, 5, 6, 0, 1, 2, 3 };',
     'n2mdp_pdown_order[N2MDP_NPSTEPS] = { 0, 1, 2, 3, 4, 5, 6 };'),
    ('gate restore removed on gate failure', '\t\tn2mdp_pwron_gates_restore();\n\t\treturn n2mdp_prot_restore() ? 8 : 7;',
     '\t\treturn n2mdp_prot_restore() ? 8 : 7;'),
    ('CG0 opened before CG1',
     '\tif (n2pw.opened1) { n2mdp_wr(N2MDP_CG1 + 8u, N2MDP_CG1_SMI); }   /* exactly enable_subsys_hwcg(SYS_MDP) */\n\tif (n2pw.opened0) { n2mdp_wr(N2MDP_CG0 + 8u, n2pw.opened0); }',
     '\tif (n2pw.opened0) { n2mdp_wr(N2MDP_CG0 + 8u, n2pw.opened0); }\n\tif (n2pw.opened1) { n2mdp_wr(N2MDP_CG1 + 8u, N2MDP_CG1_SMI); }'),
    ('verification after release removed', '\t\tif (own || !(pwr & N2MDP_MDP_BIT)) {', '\t\tif (0 && (own || !(pwr & N2MDP_MDP_BIT))) {'),
    ('display bits not excluded', 'uint32_t m = n2mdp_prot_mdp(r) & ~n2mdp_dis_bits[r];', 'uint32_t m = n2mdp_prot_mdp(r);'),
    ('legacy rule without SUB_INFRA_VDNR', 'else if (psub & n2mdp_prot_mdp(3)) { prot = true; }', 'else if (0) { prot = true; }'),
    ('domain-off check removed', 'if (!(pwr & N2MDP_MDP_BIT)) { return 2; }', 'if (0) { return 2; }'),
]


def build_run(inc, tag, tmp):
    exe = os.path.join(tmp, 't_' + tag)
    inc_dir = os.path.join(M, 'src/bsd/dev')
    r = subprocess.run(['cc', *CFLAGS, '-I' + inc_dir, '-DNORD2_MDP_INC="%s"' % inc, os.path.join(H, 't_mdp_pwron.c'), '-o', exe],
                       capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[-2000:]
    r = subprocess.run([exe], capture_output=True, text=True, timeout=600)
    last = [l for l in r.stdout.splitlines() if l.startswith(('T_MDP_PWRON', 'sweep'))]
    return r.returncode, ' | '.join(last)


def main():
    src = open(INC).read()
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = build_run(INC, 'real', tmp)
        print('real:', out)
        ok = rc == 0
        caught = 0
        for i, (name, a, b) in enumerate(MUTANTS):
            if src.count(a) != 1:
                print('mutant %d (%s): pattern not found once' % (i, name)); ok = False; continue
            p = os.path.join(tmp, 'mut%d.inc' % i)
            open(p, 'w').write(src.replace(a, b))
            # the mutant .inc lives in tmp: its relative includes resolve through -I src/bsd/dev
            rc, out = build_run(p, 'm%d' % i, tmp)
            hit = rc not in (0,)
            caught += hit
            print('mutant %d %-48s %s  (%s)' % (i, name, 'CAUGHT' if hit else 'NOT CAUGHT', (out or '').splitlines()[0][:120] if out else ''))
        print('mutants caught %d/%d' % (caught, len(MUTANTS)))
        ok = ok and caught == len(MUTANTS)
        print('RUN_T_MDP_PWRON_OK' if ok else 'RUN_T_MDP_PWRON_FAILED')
        return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
