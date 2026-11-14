import subprocess, sys, os, shutil
D, S = sys.argv[1], sys.argv[2]
src = open(f'{D}/p6/b/bsd/dev/nord2_gpu_core.c').read()
M = [
 ("prep jobs startable", "if (j->state != N2C_JOB_QUEUED || j->prep)       /* 0661", "if (j->state != N2C_JOB_QUEUED)       /* 0661"),
 ("dirty generation ignored", "if (b->dirty_gen != p->gen[k]) {", "if (0) {"),
 ("MMU fault cancels a job in prep", "j->state == N2C_JOB_QUEUED && !j->prep)      /* 0661", "j->state == N2C_JOB_QUEUED)      /* 0661"),
 ("no aging", "if (now >= j->t_submit_us && now - j->t_submit_us >= N2C_AGE_HIGH_US)", "if (0)"),
 ("no reserve", "if (free_records <= N2C_RESERVE_HIGH) {", "if (0) {"),
 ("take-back of same-or-higher class", "pick_class(c, q, tnow) < cls &&", "pick_class(c, q, tnow) <= cls &&"),
 ("precleaned ignored (double clean)", "&& !pick->precleaned) {      /* 0661", ") {      /* 0661"),
 ("mask high words dropped", "                wm[w] = r->write_mask_hi[w - 1];\n", ""),
]
os.makedirs(f'{D}/test/.mut', exist_ok=True); killed = 0
for name, a, b in M:
    assert src.count(a) == 1, name
    p = f'{D}/test/.mut/core.c'; open(p, 'w').write(src.replace(a, b)); exe = f'{D}/test/.mut/t'
    r = subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-w','-DN2C_HW_RB_EXTERN',f'-I{D}/p6/b/bsd/dev',f'-I{S}',
        f'{D}/test/core_test_0661.c', p, f'{S}/nord2_gpu_mmu.c', f'{S}/nord2_gpu_va.c','-o',exe], capture_output=True, text=True)
    if r.returncode: print('BUILD FAIL', name, r.stderr[:300]); continue
    try:
        r = subprocess.run([exe, '20000'], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), timeout=300)
        ok = 'CORE_0661_HOST_TESTS_OK' in r.stdout
        fl = [l for l in r.stderr.splitlines() if 'FAIL' in l or 'ERROR' in l][:1]
        msg = (fl[0][:130] if fl else 'rc %d' % r.returncode)
    except subprocess.TimeoutExpired:
        ok, msg = False, 'timeout (hang)'
    killed += not ok
    print(('SURVIVED ' if ok else 'killed   ') + name + ('' if ok else '  <- ' + msg))
print(f'0661 mutants killed {killed}/{len(M)}'); shutil.rmtree(f'{D}/test/.mut', ignore_errors=True)
