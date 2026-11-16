
/* ---- Chained submission: API and the visibility probe -------------------- */
static unsigned chain_env(const char *name,unsigned lo,unsigned hi,unsigned fallback){
    const char *v=getenv(name);unsigned x=0;
    if(!v||!*v)return fallback;
    for(const char *c=v;*c;c++){if(*c<'0'||*c>'9'||x>1000)return fallback;x=x*10+(unsigned)(*c-'0');}
    return x>=lo&&x<=hi?x:fallback;
}
/* One GPU check, in one 4-frame chain: clear X to c0, copy X->Y0, clear X to
 * c1, copy X->Y1 (copies: the LOAD preload program sampling X, fragment stage
 * only). Y0==c0 and Y1==c1 (Y1 equal to X's final texel, Y0 different and
 * not X's zero-initialised bytes) shows that a fragment job sees the previous
 * fragment job's writes inside one chain, also for a line its texture cache
 * read before (Y0). Anything else keeps mode 1 off (mode 2). */
static unsigned chain_probe(struct bridge *b,uintptr_t value){
    struct bo *x=NULL,*y[2]={NULL,NULL};unsigned result=3;
    b->ops.lock(b->ops.opaque);
    int e=alloc(b,16384,0,0,&x);if(!e)e=map_bo(b,x);
    for(unsigned k=0;k<2&&!e;k++){e=alloc(b,16384,0,0,&y[k]);if(!e)e=map_bo(b,y[k]);}
    if(!e){memset((void *)(uintptr_t)x->cpu,0,1024);memset((void *)(uintptr_t)y[0]->cpu,0,1024);memset((void *)(uintptr_t)y[1]->cpu,0,1024);}
    unsigned max=b->chain.max,mode=b->chain.mode,open=b->chain.open;
    b->chain.max=max<4?4:max;b->chain.mode=1;b->chain.open=1;
    b->ops.unlock(b->ops.opaque);
    static const float colour[2][4]={{0.0625f,0.25f,0.5f,1.0f},{0.75f,0.5f,0.125f,1.0f}};
    for(unsigned k=0;k<2&&!e;k++) {
        struct tegu_generic_draw_request c={.target_format=80,.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,
            .scissor={0,0,16,16},.target_token=x->handle,.target_length=x->size};
        double rgba[4]={colour[k][0],colour[k][1],colour[k][2],colour[k][3]};
        if(n2r_clear(80,rgba,c.clear_words)){e=BAD;break;}
        uint64_t out[2];unsigned n=2;e=n2mb_clear(value,&c,out,&n);if(e)break;
        struct tegu_generic_draw_request q={.target_format=80,.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,
            .scissor={0,0,16,16},.target_token=y[k]->handle,.target_length=y[k]->size,.binding_count=1};
        q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,
            .memory={.token=x->handle,.offset=0,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80,.reserved=N2MB_BINDING_STAGED};
        n=2;e=n2mb_draw(value,preload_shader(80),&q,out,&n);
    }
    b->ops.lock(b->ops.opaque);
    unsigned chained=b->chain.frames;
    int f=chain_flush(b,CHAIN_FLUSH_END);if(!e)e=f;
    if(!e)e=wait_pending(b);
    if(!e&&chained==4) {
        uint32_t px,p0,p1;memcpy(&px,(const void *)(uintptr_t)x->cpu,4);memcpy(&p0,(const void *)(uintptr_t)y[0]->cpu,4);
        memcpy(&p1,(const void *)(uintptr_t)y[1]->cpu,4);
        result=p1==px&&p0!=p1&&p0!=0?1u:2u;
        char line[160];int len=snprintf(line,sizeof(line),"NORD2_METAL chain-probe x=%08x y0=%08x y1=%08x %s\n",px,p0,p1,result==1?"visible":"stale");
        if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
            (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
    }
    b->chain.max=max;b->chain.mode=mode;b->chain.open=open;
    if(x)(void)drop(b,x);
    for(unsigned k=0;k<2;k++)if(y[k])(void)drop(b,y[k]);
    b->ops.unlock(b->ops.opaque);
    return result;
}
unsigned n2mb_chain_begin(uintptr_t value){
    struct bridge *b=(void *)value;if(!b)return 0;
    b->ops.lock(b->ops.opaque);
    unsigned mode=b->chain.mode,probe=b->chain.probe==1&&mode==1&&!b->broken;
    if(probe)b->chain.probe=2;
    b->ops.unlock(b->ops.opaque);
    if(probe) {
        unsigned r=chain_probe(b,value);
        b->ops.lock(b->ops.opaque);b->chain.st[9]=r;if(r!=1&&b->chain.mode==1)b->chain.mode=2;b->ops.unlock(b->ops.opaque);
    }
    b->ops.lock(b->ops.opaque);
    if(b->chain.mode&&!b->broken)b->chain.open=1;
    mode=b->chain.open?b->chain.mode:0;
    b->ops.unlock(b->ops.opaque);return mode;
}
int n2mb_chain_end(uintptr_t value,uint64_t *job){
    struct bridge *b=(void *)value;if(!b)return BAD;
    b->ops.lock(b->ops.opaque);
    unsigned had=b->chain.frames;
    int e=chain_flush(b,CHAIN_FLUSH_END);
    if(job)*job=had&&!e?b->chain.last_job:0;
    b->chain.open=0;
    if(b->chain.mode&&!(++b->chain.ends%120u))chain_log(b);
    b->ops.unlock(b->ops.opaque);return e;
}
unsigned n2mb_chain_mode(uintptr_t value,unsigned mode,unsigned frames){
    struct bridge *b=(void *)value;if(!b)return 0;
    b->ops.lock(b->ops.opaque);unsigned old=b->chain.mode;
    if(b->chain.frames)(void)chain_flush(b,CHAIN_FLUSH_OTHER);
    if(mode<=2){b->chain.mode=mode;b->chain.probe=2;}
    if(frames>=2&&frames<=N2MB_CHAIN_MAX_FRAMES)b->chain.max=frames;
    b->ops.unlock(b->ops.opaque);return old;
}
void n2mb_chain_stats(uintptr_t value,uint64_t out[N2MB_CHAIN_STATS]){
    struct bridge *b=(void *)value;if(!b||!out)return;
    b->ops.lock(b->ops.opaque);memcpy(out,b->chain.st,sizeof(b->chain.st));out[10]=b->chain.mode;b->ops.unlock(b->ops.opaque);
}
