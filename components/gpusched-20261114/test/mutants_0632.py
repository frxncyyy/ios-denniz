import subprocess, sys, os, shutil
D, S = sys.argv[1], sys.argv[2]
src = open(f'{D}/b2/bsd/dev/nord2_gpu_core.c').read()
M = [
 ("conflict ignores the other job's writes", "                        if (aw || ((b->write_mask >> k) & 1u))", "                        if (aw)"),
 ("no conflict check", "                if (jobs_conflict(o, j) || jobs_conflict(j, o)) {", "                if (0) {"),
 ("overtake older compute jobs too", "if (!o->precise || o->kind != N2C_JOB_FRAGMENT ||", "if (!o->precise ||"),
 ("overtake for non-precise older jobs", "if (!o->precise || o->kind != N2C_JOB_FRAGMENT ||", "if (o->kind != N2C_JOB_FRAGMENT ||"),
 ("precise despite a full list", "j->precise = (r->flags & N2C_SUBMIT_PRECISE) && !j->full_maint;", "j->precise = (r->flags & N2C_SUBMIT_PRECISE);"),
 ("fragment jobs may overtake", "if (!c->pipe || !j->precise || j->kind != N2C_JOB_COMPUTE)", "if (!c->pipe || !j->precise)"),
 ("pipe switch ignored", "if (!c->pipe || !j->precise || j->kind != N2C_JOB_COMPUTE)", "if (!j->precise || j->kind != N2C_JOB_COMPUTE)"),
 ("untouched buffers cleaned", "continue;       /* 0632: listed for lifetime only; the chain that touches it cleans it */", ";"),
]
os.makedirs(f'{D}/test/.mut', exist_ok=True); killed = 0
for name, a, b in M:
    assert src.count(a) == 1, name
    p = f'{D}/test/.mut/core.c'; open(p, 'w').write(src.replace(a, b)); exe = f'{D}/test/.mut/t'
    r = subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-w','-DN2C_HW_RB_EXTERN',f'-I{D}/b2/bsd/dev',f'-I{S}',
        f'{D}/test/core_test_0632.c', p, f'{S}/nord2_gpu_mmu.c', f'{S}/nord2_gpu_va.c','-o',exe], capture_output=True, text=True)
    if r.returncode: print('BUILD FAIL', name, r.stderr[:300]); continue
    r = subprocess.run([exe, '20000'], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), timeout=900)
    ok = 'CORE_0632_HOST_TESTS_OK' in r.stdout; killed += not ok
    fl = [l for l in r.stderr.splitlines() if 'FAIL' in l or 'ERROR' in l][:1]
    print(('SURVIVED ' if ok else 'killed   ') + name + ('' if ok else '  <- ' + (fl[0][:140] if fl else 'rc %d' % r.returncode)))
print(f'0632 mutants killed {killed}/{len(M)}'); shutil.rmtree(f'{D}/test/.mut', ignore_errors=True)
