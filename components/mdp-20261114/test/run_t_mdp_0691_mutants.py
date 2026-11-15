#!/usr/bin/env python3
"""0691 host mutants: t_mdp_full_0691.c on the real 0691 sources (must pass) and on mutants (each must FAIL).
usage: python3 run_t_mdp_0691_mutants.py [SRC_ROOT]   (default: ../piece-0691/patched-sources-0691)"""
import os, shutil, subprocess, sys, tempfile
H = os.path.dirname(os.path.abspath(__file__)); M = os.path.dirname(H)
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(M, 'piece-0691/patched-sources-0691')
CF = ['-std=c11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-Wall', '-Wextra',
      '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-misleading-indentation']
INC, JOB = 'bsd/dev/nord2_mdp.inc', 'bsd/dev/nord2_mdp_job.h'
MUT = [
 ('no SW0_RST_B lane reset', INC, "	n2mdp_wr(N2MDP_SW0_RST_B, ~bits);\n", ""),
 ('WROT release not polled', INC, "	w_ok = n2mdp_poll_bits(W + W_SOFT_RST_STAT, 1u, 0u, 100) && w_ok;\n", ""),
 ('scaled NV12 dst allowed', INC, "		if (n2mf_is_yuv(dst->fmt)) { return ENOTSUP; }            /* scaled NV12 destinations: never run by a boot job */\n", ""),
 ('scale tile limit not applied', INC, "		if (t16 < 32u) { j.tile_w = (uint8_t)t16; tpx = t16 * 16u; }\n", ""),
 ('no retry after recovery', INC, "		err = n2mdp_run_x(pg, &xs);\n	}", "	}"),
 ('dead pipe 2 still used for dual', INC, "&& !(n2mdp_lane_dead & 2u)) {", ") {"),
 ('scale_off never set', INC, "		n2mdp_scale_off = 1;\n", ""),
 ('big frames without SCALE_BIG', INC, "{ need |= N2MDP_CAP_SCALE_BIG; }", "{ }"),
 ('fault drop unmaps busy entries', INC, "if (n2mdp_mc[i].busy) { n2mdp_mc[i].dead = true; continue; }", "if (0) { continue; }"),
 ('eviction ignores busy', INC, "if (!n2mdp_mc[i].busy && n2mdp_mc[i].lru < best)", "if (n2mdp_mc[i].lru < best)"),
 ('pipe-2 direct MOUT slot 0', JOB, "N2W(p, N2MDP_T_RDMA2_MOUT, 1u << 1);          /* RDMA2 out slot 1 = WROT2 */", "N2W(p, N2MDP_T_RDMA2_MOUT, 1u << 0);"),
 ('pipe-2 direct WROT2 SEL 0', JOB, "N2W(p, N2MDP_T_WROT2_SEL, 1u);                /* WROT2 in slot 1 = RDMA2 */", "N2W(p, N2MDP_T_WROT2_SEL, 0u);"),
 ('larb ports of pipe 0 for pipe 2', INC, "unsigned pr = pipe ? 1u : 0u, pw = pipe ? 3u : 2u;", "unsigned pr = 0u, pw = 2u;"),
 ('no fallback to pipe 2', INC, "if (n2mdp_pipe_trylock(1)) { pi = 1; goto locked; }", "if (0) { pi = 1; goto locked; }"),
 ('dual job steps only pipe 0', INC, "if (!db) { db = n2mdp_x_step(&xb) != 0; }", "if (!db) { db = true; }"),
 ('pipe2 flag ignored in compile', JOB, "uint32_t pipe = (scaled || j->pipe2) ? N2MDP_PIPE_SCALE : N2MDP_PIPE_DIRECT;", "uint32_t pipe = scaled ? N2MDP_PIPE_SCALE : N2MDP_PIPE_DIRECT;"),
 ('temporary mapping never unmapped', INC, "if (slot >= N2MDP_MC_N) { n2mdp_iova_unmap(iova, n); return; }", "if (slot >= N2MDP_MC_N) { return; }"),
 ('busy mask not cleared', INC, "__atomic_fetch_and(&n2mdp_busy_mask, ~(1u << pi), __ATOMIC_ACQ_REL);", ""),
 ('sysctl tile ignored', INC, "if (t16 >= 4u && t16 <= 32u) { j.tile_w = (uint8_t)t16; tpx = t16 * 16u; }", "(void)t16;"),
]
def build_run(root, tag):
    exe = os.path.join(tempfile.gettempdir(), 't90m_' + tag)
    r = subprocess.run(['cc'] + CF + ['-DNORD2_MDP_INC="%s"' % os.path.join(root, INC), '-o', exe, os.path.join(H, 't_mdp_full_0691.c')], capture_output=True, text=True)
    if r.returncode: return 'BUILD-FAIL'
    r = subprocess.run([exe], capture_output=True, text=True, timeout=600)
    return 'OK' if 'T_MDP_FULL ALL OK' in r.stdout else 'FAILED'
res = build_run(ROOT, 'real'); print('real sources:', res); ok = res == 'OK'
for name, f, a, b in MUT:
    d = tempfile.mkdtemp(); shutil.copytree(ROOT, d, dirs_exist_ok=True)
    p = os.path.join(d, f); s = open(p).read()
    if s.count(a) != 1: print('MUTANT PATTERN MISSING:', name); ok = False; continue
    open(p, 'w').write(s.replace(a, b)); res = build_run(d, 'm'); shutil.rmtree(d)
    caught = res != 'OK'; ok &= caught; print('%-40s %s' % (name, 'caught' if caught else 'NOT CAUGHT'))
print('MUTANTS ALL CAUGHT' if ok else 'MUTANTS: PROBLEM'); sys.exit(0 if ok else 1)
