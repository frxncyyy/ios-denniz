#!/usr/bin/env python3
"""Full-screen job census over the frame_bench host replay (host only, no GPU).

Builds frame_bench's exact prelude (real frontend + bridge + fake JM kernel) with
a copy of frame_bench_main.m that additionally prints, for the first steady
batched frame of each scenario:
  FBX_JOB  one line per submitted JM job: framebuffer size in tiles, draws in
           the chain, chained preload job, pre-frame mode, clean tile write,
           tiles covered by the union of the draws' scissor boxes (an upper
           bound of what an INTERSECT pre-frame preload touches)
  FBX_BREAKS the frontend's batch-break reasons for the frame
Usage: fsjobs.py [--scenario NAME] [--preframe 0|1] [--rebuild]
Outputs persist in this directory (fsjobs-<tag>.log/json).
"""
import argparse, json, os, pathlib, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
GPU = HERE.parents[2]
sys.path.insert(0, str(GPU / 'tools'))
import frame_bench as fb  # noqa: E402

HOOK = r'''
static int fbx_on;static const char *fbx_scn;static unsigned fbx_job_no;
static void fbx_job(struct fake *f,const struct nord2_gpu_submit_request *r,struct buffer *c,unsigned steps){
 (void)f;if(!fbx_on)return;
 uint32_t w1,w0f;memcpy(&w1,c->data+N2R_FRAGMENT+36,4);memcpy(&w0f,c->data+N2R_FRAGMENT+32,4);
 unsigned tx=(w1&0xfff)+1,ty=((w1>>16)&0xfff)+1;
 unsigned ftiles=(tx-(w0f&0xfff))*(ty-((w0f>>16)&0xfff)); /* fragment-job bound (n2mb_extent) */
 uint32_t fbw0;memcpy(&fbw0,c->data,4);
 uint32_t rtw1;memcpy(&rtw1,c->data+128+4,4);
 static unsigned char map[4096*4096/256];if(tx*ty>sizeof(map))return;memset(map,0,tx*ty);
 uint64_t h=r->heads[0];unsigned draws=0,preload=0,first=1,invisible=0;
 while(h){
  unsigned off=(unsigned)(h-c->va),blk=off/N2R_BLOCK;const uint8_t *jh=c->data+off;
  if(first&&blk!=0)preload=1;
  else{
   uint32_t a,b;memcpy(&a,jh+104,4);memcpy(&b,jh+108,4);
   unsigned x0=a&0xffff,y0=a>>16,x1=b&0xffff,y1=b>>16;draws++;
   if(x1<x0||y1<y0)invisible++;
   else for(unsigned y=y0/16;y<=y1/16&&y<ty;y++)for(unsigned x=x0/16;x<=x1/16&&x<tx;x++)map[y*tx+x]=1;
   /* per draw: scissor area (upper bound of covered pixels) and the fragment
    * program address of this block's SPD (block+1600, binary at +8) */
   const uint8_t *blkp=c->data+(size_t)blk*N2R_BLOCK;
   printf("FBX_DRAW scn=%s job=%u px=%llu fs=%llx\n",fbx_scn,fbx_job_no,
    x1<x0||y1<y0?0ull:(unsigned long long)(x1-x0+1)*(y1-y0+1),(unsigned long long)get64(blkp+1600+8));
  }
  first=0;h=get64(jh+24);
 }
 unsigned covered=0;for(unsigned i=0;i<tx*ty;i++)covered+=map[i];
 printf("FBX_JOB scn=%s job=%u tiles_x=%u tiles_y=%u tiles=%u covered=%u draws=%u invisible=%u preload=%u preframe=%u clean=%u rt=%llx steps=%u handles=%u ftiles=%u\n",
  fbx_scn,fbx_job_no,tx,ty,tx*ty,covered,draws,invisible,preload,fbw0&7,rtw1>>31,(unsigned long long)get64(c->data+128+32),steps,r->nr_handles,ftiles);
 {uint32_t row,w0b;memcpy(&row,c->data+128+32+8,4);memcpy(&w0b,c->data+N2R_FRAGMENT+32,4);(void)w0b;
  printf("FBX_JOBX scn=%s job=%u row=%u fbw=%u fbh=%u\n",fbx_scn,fbx_job_no,row,tx*16,ty*16);}
 fbx_job_no++;
}
'''


def patched_source():
    src = fb.gen_source(False)
    def once(s, old, new):
        assert s.count(old) == 1, (old[:60], s.count(old))
        return s.replace(old, new, 1)
    src = once(src, 'static int call(void *p,unsigned s,', HOOK + 'static int call(void *p,unsigned s,')
    if '==N2R_JOB&&steps<64);' in src:   # older bridge test: allow chains beyond 64
        src = once(src, '==N2R_JOB&&steps<64);', '==N2R_JOB&&steps<1024);')
    src = once(src, 'assert(seen0&&steps>=1);f->last_chain=steps;', 'assert(seen0&&steps>=1);f->last_chain=steps;fbx_job(f,r,c,steps);')
    src = once(src, 'unsigned line=c->invalidLine;\n [c commit];[c waitUntilCompleted];',
               'unsigned line=c->invalidLine;\n fbx_on=!strcmp(mode,"batched")&&!strcmp(phase,"steady")&&idx==0;fbx_scn=s->name;\n'
               ' [c commit];[c waitUntilCompleted];fbx_on=0;')
    src = once(src, ' fflush(stdout);\n}',
               ' if(!strcmp(mode,"batched")&&!strcmp(phase,"steady")&&idx==0){printf("FBX_BREAKS scn=%s",s->name);'
               'for(unsigned r_=0;r_<TEGU_BREAK_REASONS;r_++)if(pb->breaks[r_])printf(" %s=%u",tegu_break_names[r_],pb->breaks[r_]);'
               'printf(" encoders=%u fs_load=%u fs_clear=%u off_load=%u off_clear=%u other=%u kjobs=%llu\\n",pb->encoders,pb->fullLoad,'
               'pb->fullClear,pb->offLoad,pb->offClear,pb->otherJobs,(unsigned long long)(b.submits-a.submits));}\n fflush(stdout);\n}')
    src = once(src, ' tegu_jm_batch_force=-1;\n printf("FB_DONE\\n");',
               ' tegu_jm_batch_force=-1;\n {TeguRenderPipeline *ps[4]={fb_pf,fb_pl,fb_pd,fb_pn};const char *pn[4]={"fixed","luma","down","narrow"};\n'
               '  for(unsigned i=0;i<4;i++)for(unsigned v=0;v<ps[i]->genericVariantCount;v++){struct tegu_render_generic_variant *x=&ps[i]->genericVariants[v];\n'
               '   printf("FBX_VARIANT pipe=%s rt=%u options=%x reads_fb=%u shader_flags=%x\\n",pn[i],x->rt,x->options_flags,x->compile.reads_framebuffer,x->shader.flags);}}\n'
               ' printf("FB_DONE\\n");')
    return src


# frame_bench pipelines, identified by the SPIR-V fragment size in TEGU_PSO_MISS
# (fixed_frag_lph_cph, luma_boost_lph, downsample_4_frag_lph, narrow_blur_11_frag_lph).
FB_PIPES = {593516: 'fixed', 6136: 'luma', 9436: 'downsample', 14896: 'narrow'}


def shader_stats(text):
    """Mesa shaderdb FRAGMENT stats (BIFROST_MESA_DEBUG=shaderdb) keyed by pipeline; the
    stats line precedes the TEGU_PSO_MISS of its pair; the last compile wins (tile variant)."""
    import re
    out, last = {}, None
    for line in text.splitlines():
        if 'MESA_SHADER_FRAGMENT shader:' in line:
            kv = dict((k, float(v)) for v, k in re.findall(r'([0-9.]+) (fma|cvt|sfu|v|t|ls|cycles|instrs|threads)\b', line))
            last = kv
        m = re.search(r'TEGU_PSO_MISS mali .* fs=(\d+)', line)
        if m and last is not None and int(m.group(1)) in FB_PIPES:
            out[FB_PIPES[int(m.group(1))]] = last
            last = None
    return out


def build_census(stdout, a):
    """Per-job census for frame_model.py --pixel: target size/bpp, tiles, scissor
    coverage, preload/pre-frame mode and per-draw pixels with the Mesa fragment stats."""
    jobs, draws = {}, {}
    for line in stdout.splitlines():
        if line.startswith(('FBX_JOB ', 'FBX_JOBX ', 'FBX_DRAW ')):
            kv = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
            key = (kv['scn'], int(kv['job']))
            if line.startswith('FBX_DRAW'):
                draws.setdefault(key, []).append({'px': int(kv['px']), 'fs': kv['fs']})
            else:
                jobs.setdefault(key, {}).update(kv)
    stats = shader_stats(stdout)
    # fragment program per target class (frame_bench shapes): see FB_PIPES
    by_class = {}
    for key, j in jobs.items():
        cls = {(9, 14): 'downsample', (36, 77): 'narrow', (19, 13): 'fixed'}.get((int(j['tiles_x']), int(j['tiles_y'])))
        if cls:
            for d in draws.get(key, []):
                by_class.setdefault(d['fs'], cls)
    out = {'tag': a.tag, 'preframe': int(a.preframe), 'tile_fetch': bool(a.tile), 'shader_stats': stats, 'scenarios': {}}
    for key in sorted(jobs):
        j = jobs[key]
        fbw, row = int(j['fbw']), int(j['row'])
        bpp = max(1, round(row / fbw)) if fbw else 4
        ds = []
        for d in draws.get(key, []):
            name = by_class.get(d['fs'], 'luma')
            ds.append({'px': d['px'], 'shader': name})
        out['scenarios'].setdefault(key[0], []).append({
            'tiles': int(j['tiles']), 'ftiles': int(j.get('ftiles', j['tiles'])), 'covered': int(j['covered']), 'bpp': bpp, 'preload': int(j['preload']),
            'preframe': int(j['preframe']), 'clean': int(j['clean']), 'draws': ds})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--scenario', default='')
    ap.add_argument('--preframe', default='0')
    ap.add_argument('--rebuild', action='store_true')
    ap.add_argument('--tag', default='current')
    ap.add_argument('--batch-max', type=int, default=0, help='experiment: shadow-copy N2MB_BATCH_MAX')
    ap.add_argument('--src-patch', action='append', default=[], help='experiment: JSON [old, new] applied to the generated replay source')
    ap.add_argument('--patch', action='append', default=[], help='experiment: JSON [rel_path_under_metal, old, new] applied to the shadow copy')
    ap.add_argument('--tile', action='store_true',
                    help='experiment: -DTEGU_TILE_FETCH_DEFAULT and a build-local compositor_submit.inc that lets LD_TILE draws batch')
    a = ap.parse_args()
    out = HERE / ('build-' + a.tag)
    out.mkdir(exist_ok=True)
    binary = out / 'fsjobs_bin'
    if a.rebuild or not binary.exists():
        fb.BIN = out
        # Experiments patch a shadow copy of gpu/metal (production files untouched).
        import shutil
        shadow = out / 'shadow'
        if shadow.exists():
            shutil.rmtree(shadow)
        shadow.mkdir()
        shutil.copytree(pathlib.Path(os.environ.get("SRC_METAL", str(GPU / "metal"))), shadow / "metal")
        for name in ('src', 'tests', 'include', 'build'):
            (shadow / name).symlink_to(GPU / name)
        def patch(rel, old, new, optional=False):
            f = shadow / 'metal' / rel
            t = f.read_text()
            if optional and old not in t:
                return   # already applied upstream
            assert t.count(old) == 1, (rel, old)
            f.write_text(t.replace(old, new))
        if a.batch_max:
            patch('nord2_metal_bridge.h', '#define N2MB_BATCH_MAX 32u', '#define N2MB_BATCH_MAX %du' % a.batch_max)
        if a.tile:
            patch('tegu/compositor_submit.inc', ' if(tegu_generic_job_shader(j)->flags&TEGU_GENERIC_SHADER_TILE_FETCH)return TEGU_JM_BREAK_READS;\n', '', True)
            patch('tegu/compositor_submit.inc', 'if(kind==TEGU_GENERIC_DRAW_ATTACHMENT||kind==TEGU_GENERIC_DRAW_TILE_ATTACHMENT)return TEGU_JM_BREAK_READS;',
                  'if(kind==TEGU_GENERIC_DRAW_ATTACHMENT)return TEGU_JM_BREAK_READS;', True)
        for pf in a.patch:
            rel, old, new_ = json.load(open(pf))
            patch(rel, old, new_)
        fb.P, fb.TEGU = shadow, shadow / 'metal/tegu'
        src = patched_source()
        for pf in a.src_patch:
            old_, new_ = json.load(open(pf))
            assert src.count(old_) == 1, old_
            src = src.replace(old_, new_)
        (out / 'frame_bench.m').write_text(src)
        (out / 'hostbase_real_nord2_metal_bridge.c').write_bytes((fb.P / 'metal/nord2_metal_bridge.c').read_bytes())
        (out / 'nord2_metal_bridge.h').write_bytes((fb.P / 'metal/nord2_metal_bridge.h').read_bytes())
        cmd = ['nice', '-n', '10', 'clang', '-fblocks', '-pthread', '-O1', '-g1', '-Wno-incompatible-pointer-types',
               '-Wno-incompatible-function-pointer-types', '-w', '-fconstant-string-class=NXConstantString',
               '-DTEGU_HOST_REAL_V2', '-DTEGU_INERT_PIPELINES', '-DC6F_ATTACHMENTS=2', '-DTEGU_JM_BATCH_DEFAULT', '-DFB_HAVE_ASYNC=1'] + (['-DTEGU_TILE_FETCH_DEFAULT'] if a.tile else [])
        cmd += ['-I' + str(p) for p in (out, fb.TEGU, fb.P / 'metal', fb.P / 'include', GPU / 'build/tegu-frontend')]
        cmd += [str(out / 'frame_bench.m'), str(out / 'hostbase_real_nord2_metal_bridge.c'), str(fb.P / 'metal/nord2_metal_render.c'),
                str(fb.P / 'src/nord2_gpu_desc.c'), str(fb.P / 'src/nord2_gpu_jm.c')]
        cmd += ['-lobjc', '-ldl', '-lBlocksRuntime', '-Wl,--wrap=n2mb_copy'] + ['-L' + str(p) for p in fb.LIBS]
        cmd += ['-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(binary)]
        t = time.time()
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        (out / 'build.log').write_text(' '.join(cmd) + '\n' + r.stderr)
        if r.returncode:
            sys.exit('build failed:\n' + r.stderr[-3000:])
        print('built in %.0fs' % (time.time() - t))
    env = dict(os.environ, LD_LIBRARY_PATH=':'.join(map(str, fb.LIBS)), TEGU_PLAN_MEMO_VERIFY='0', FB_ASYNC='0',
               NORD2_METAL_PREFRAME=a.preframe, BIFROST_MESA_DEBUG='shaderdb')
    cmd = ['nice', '-n', '10', str(binary)] + fb.corpus_args() + ['1'] + ([a.scenario] if a.scenario else [])
    r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = HERE / ('fsjobs-%s-pf%s.log' % (a.tag, a.preframe))
    log.write_text(r.stdout)
    jobs, breaks = {}, {}
    for line in r.stdout.splitlines():
        if line.startswith(('FBX_JOB ', 'FBX_BREAKS ')):
            kv = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
            (jobs.setdefault(kv['scn'], []).append(kv) if line.startswith('FBX_JOB') else breaks.__setitem__(kv['scn'], kv))
    res = {}
    for scn, js in jobs.items():
        full = [j for j in js if int(j['tiles']) >= 10000]
        res[scn] = dict(jobs=len(js), full=len(full), other=len(js) - len(full),
                        full_draws=[int(j['draws']) for j in full], full_preload=sum(int(j['preload']) for j in full),
                        full_cover=[round(int(j['covered']) / int(j['tiles']), 3) for j in full],
                        other_desc=['%sx%s d%s' % (j['tiles_x'], j['tiles_y'], j['draws']) for j in js if int(j['tiles']) < 10000],
                        breaks={k: v for k, v in breaks.get(scn, {}).items() if k != 'scn'})
    (HERE / ('fsjobs-%s-pf%s.json' % (a.tag, a.preframe))).write_text(json.dumps(res, indent=1))
    census = build_census(r.stdout, a)
    if census:
        (HERE / ('census-%s-pf%s.json' % (a.tag, a.preframe))).write_text(json.dumps(census, indent=1))
    for scn, v in res.items():
        print(scn, json.dumps(v))
    if r.returncode or 'FB_DONE' not in r.stdout:
        print('replay rc=%d' % r.returncode, r.stdout[-2000:])


if __name__ == '__main__':
    main()
