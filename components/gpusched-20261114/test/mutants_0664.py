import subprocess, sys, os, shutil
D, S = sys.argv[1], sys.argv[2]
src = open(f'{D}/p7/b/bsd/dev/nord2_gpu_core.c').read()
M = [
 ("start ignores CPU cleans", "if ((b->flags & N2C_BO_IMPORTED) || b->clean_seq > c->l2_done_seq)\n                                        need_inv = true;", "if ((b->flags & N2C_BO_IMPORTED))\n                                        need_inv = true;"),
 ("start ignores imports", "if ((b->flags & N2C_BO_IMPORTED) || b->clean_seq > c->l2_done_seq)\n                                        need_inv = true;", "if (b->clean_seq > c->l2_done_seq)\n                                        need_inv = true;"),
 ("end skip ignores CPU-visible buffers", "if ((b->flags & N2C_BO_IMPORTED) || b->cpu_visible)\n                                        keep_end = false;", "if ((b->flags & N2C_BO_IMPORTED))\n                                        keep_end = false;"),
 ("no L2 clean before CPU visibility", "if (b->l2_dirty && c->gpu_powered) {", "if (0) {"),
 ("no L2 clean when visibility changed mid-run", "if (now_visible && c->gpu_powered) {", "if (0) {"),
 ("l2_done_seq from a faulted chain too", "if (state == N2C_JOB_DONE && j->start_inv && j->inv_seq > c->l2_done_seq)", "if (j->start_inv && j->inv_seq > c->l2_done_seq && state != N2C_JOB_CANCELLED)"),
 ("flush id from submit time (stale)", "                        pick->flush_id = n2c_latest_flush(c->hw);\n                        pick->flush_id_valid = true;", "                        if (!pick->flush_id_valid) { pick->flush_id = 0; pick->flush_id_valid = true; }"),
]
os.makedirs(f'{D}/test/.mut', exist_ok=True); killed = 0
for name, a, b in M:
    assert src.count(a) == 1, name
    p = f'{D}/test/.mut/core.c'; open(p, 'w').write(src.replace(a, b)); exe = f'{D}/test/.mut/t'
    r = subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-w','-DN2C_HW_RB_EXTERN',f'-I{D}/p7/b/bsd/dev',f'-I{S}',
        f'{D}/test/core_test_0664.c', p, f'{S}/nord2_gpu_mmu.c', f'{S}/nord2_gpu_va.c','-o',exe], capture_output=True, text=True)
    if r.returncode: print('BUILD FAIL', name, r.stderr[:300]); continue
    try:
        r = subprocess.run([exe, '20000'], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), timeout=300)
        ok = 'CORE_0664_HOST_TESTS_OK' in r.stdout
        fl = [l for l in r.stderr.splitlines() if 'FAIL' in l or 'ERROR' in l][:1]
        msg = (fl[0][:130] if fl else 'rc %d' % r.returncode)
    except subprocess.TimeoutExpired:
        ok, msg = False, 'timeout (hang)'
    killed += not ok
    print(('SURVIVED ' if ok else 'killed   ') + name + ('' if ok else '  <- ' + msg))
print(f'0664 mutants killed {killed}/{len(M)}'); shutil.rmtree(f'{D}/test/.mut', ignore_errors=True)
