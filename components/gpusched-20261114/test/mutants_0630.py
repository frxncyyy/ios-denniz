import subprocess, sys, os
D, S = sys.argv[1], sys.argv[2]
src = open(f'{D}/b/bsd/dev/nord2_gpu_core.c').read()
M = [
 ("nr_done ignores the current chain", "int nr_done = submitted - (ev[i].cur_active ? 1 : 0) -", "int nr_done = submitted - 0 -"),
 ("nr_done ignores the NEXT chain", "- (ev[i].next_busy && c->next[s] ? 1 : 0);", "- 0;"),
 ("untargeted stop", "n2hw_js_stop_chain(c->hw, j->slot, true, j->chain_flag);", "n2hw_js_stop(c->hw, j->slot, true);"),
 ("same-context NEXT not limited to the second oldest", "(oldest[j->ctx] == c->running[slot] && second[j->ctx] == j->id)", "(oldest[j->ctx] == c->running[slot])"),
 ("NEXT behind a render job", "f->in_next || f->kind == N2C_JOB_RENDER)", "f->in_next)"),
 ("evicted chain left RUNNING", "q->state = N2C_JOB_QUEUED;      /* never started", "q->state = N2C_JOB_RUNNING;      /* never started"),
 ("deadline on a NEXT chain", "j->state == N2C_JOB_RUNNING && !j->in_next &&", "j->state == N2C_JOB_RUNNING &&"),
 ("no eviction head given on failure", "nh[s] = q ? q->heads[q->phase] : 0;", "nh[s] = 0;"),
 ("NEXT behind a stopping chain", "if (!f || f->state != N2C_JOB_RUNNING ||", "if (!f ||"),
 ("promoted job keeps in_next", "                up->in_next = false;\n                c->lat.nx_promoted++;", "                c->lat.nx_promoted++;"),
]
os.makedirs(f'{D}/test/.mut', exist_ok=True)
killed = 0
for name, a, b in M:
    assert src.count(a) == 1, name
    p = f'{D}/test/.mut/core.c'; open(p, 'w').write(src.replace(a, b))
    exe = f'{D}/test/.mut/t'
    r = subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-w','-DN2C_HW_RB_EXTERN',f'-I{D}/b/bsd/dev',f'-I{S}',
        f'{D}/test/core_test_0630.c', p, f'{S}/nord2_gpu_mmu.c', f'{S}/nord2_gpu_va.c','-o',exe], capture_output=True, text=True)
    if r.returncode: print('BUILD FAIL', name, r.stderr[:300]); continue
    r = subprocess.run([exe, '20000'], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), timeout=600)
    ok = 'CORE_0630_HOST_TESTS_OK' in r.stdout
    killed += not ok
    fl = [l for l in r.stderr.splitlines() if 'FAIL' in l or 'ERROR' in l][:1]
    print(('SURVIVED ' if ok else 'killed   ') + name + ('' if ok else '  <- ' + (fl[0][:150] if fl else 'rc %d' % r.returncode)))
print(f'mutants killed {killed}/{len(M)}')
import shutil; shutil.rmtree(f'{D}/test/.mut', ignore_errors=True)
