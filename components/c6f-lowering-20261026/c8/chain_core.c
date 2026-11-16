/* ---- Chained submission (n2mb_chain_begin/end) ---------------------------
 * One kernel job for a run of frames: the kernel starts JS1 at heads[0] and,
 * once that chain completed, JS0 at heads[1]; the hardware follows each job
 * header's Next. Every frame keeps its own blocks and tiler context; its tiler
 * jobs are renumbered after the previous frame's (and depend on its last), its
 * fragment job gets the next JS0 index, a dependency on the previous fragment
 * job and the barrier, so frame k+1's fragments start after frame k's
 * completed. All tiler jobs run before any fragment job: each frame has its
 * own tiler-heap slice (a full HEAP_DEFAULT heap, as an unchained job), and a
 * frame whose vertex stage may read what an earlier frame of the chain writes
 * cannot join (the tiler would read it before that fragment job ran). */
static void chain_put64(uint8_t *p,uint64_t v){memcpy(p,&v,8);}
static uint64_t chain_get64(const uint8_t *p){uint64_t v;memcpy(&v,p,8);return v;}
static int chain_written(const struct bridge *b,uint64_t handle){
    const struct nord2_gpu_submit_request *r=&b->chain.req;
    for(unsigned i=0;b->chain.frames&&i<r->nr_handles;i++)if(r->handles[i]==handle)return (r->write_mask>>i)&1u;
    return 0;
}
static void chain_log(struct bridge *b){
    const uint64_t *st=b->chain.st;char line[240];
    int len=snprintf(line,sizeof(line),"TEGU_PERF_CHAIN mode=%u max=%u chains=%llu frames=%llu dep_links=%llu flush_dep=%llu "
     "flush_handles=%llu flush_full=%llu flush_other=%llu flush_end=%llu most=%llu probe=%llu\n",b->chain.mode,b->chain.max,
     (unsigned long long)st[0],(unsigned long long)st[1],(unsigned long long)st[2],(unsigned long long)st[3],(unsigned long long)st[4],
     (unsigned long long)st[5],(unsigned long long)st[6],(unsigned long long)st[7],(unsigned long long)st[8],(unsigned long long)st[9]);
    if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
        (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
}
static int chain_flush(struct bridge *b,unsigned reason){
    if(!b->chain.frames)return 0;
    struct nord2_gpu_submit_request req=b->chain.req;unsigned frames=b->chain.frames;
    struct bo *blocks[N2MB_CHAIN_MAX_FRAMES];unsigned nblocks=b->chain.nblocks;memcpy(blocks,b->chain.blocks,sizeof(blocks));
    int slots[2*N2MB_CHAIN_MAX_FRAMES];unsigned nslots=b->chain.nslots;memcpy(slots,b->chain.slots,sizeof(slots));
    /* Reset first: a release inside the submit path must not re-enter. */
    b->chain.frames=0;b->chain.used=0;b->chain.tn=b->chain.fn=0;b->chain.ttail=b->chain.ftail=NULL;
    b->chain.nblocks=b->chain.nslots=0;memset(&b->chain.req,0,sizeof(b->chain.req));
    if(reason<N2MB_CHAIN_STATS)b->chain.st[reason]++;
    uint64_t job=0;int e=jm_submit(b,&req,0,&job);
    if(!e){b->chain.st[0]++;b->chain.st[1]+=frames;if(frames>b->chain.st[8])b->chain.st[8]=frames;}
    b->chain.last_job=e?0:job;
    /* The blocks and colour slots belong to this job; on a refused submit
     * nothing ran and they are free again (unless the bridge broke). */
    uint64_t stamp=e?(b->broken?UINT64_MAX:0):job;
    for(unsigned k=0;k<nblocks;k++)for(unsigned i=0;i<b->cmdpool_n;i++)if(b->cmdpool[i].o==blocks[k])b->cmdpool[i].job=stamp;
    for(unsigned k=0;k<nslots;k++)if(slots[k]>=0&&slots[k]<256)b->clear_slot_job[slots[k]]=stamp;
    return e;
}
/* Before building a frame: the tiler-heap VA it uses and the BO to list.
 * A frame joins only when the chain is open and the frame is plain (no
 * snapshot, thread storage, diagnostics or synchronous trace). */
static struct bo *chain_heap(struct bridge *b,unsigned eligible,uint64_t *heap_va){
    b->chain.slice_now=-1;*heap_va=b->heap->va;
    if(!eligible||!b->chain.open||!b->chain.mode||b->tiler.heap_bytes!=HEAP_DEFAULT)return b->heap;
    if(!b->chain.heap) {
        b->chain.slice=HEAP_DEFAULT;
        if(alloc(b,(uint64_t)b->chain.max*b->chain.slice,ALLOC_GPU_ONLY,1,&b->chain.heap)){b->chain.heap=NULL;b->chain.mode=0;return b->heap;}
    }
    uint32_t full=(1u<<b->chain.max)-1u;
    if((b->chain.used&full)==full&&chain_flush(b,CHAIN_FLUSH_FULL))return b->heap;
    unsigned s=0;while(s<b->chain.max&&(b->chain.used>>s)&1u)s++;
    if(s>=b->chain.max)return b->heap;
    b->chain.slice_now=(int)s;*heap_va=b->chain.heap->va+(uint64_t)s*b->chain.slice;return b->chain.heap;
}
/* After building the frame into `cmd` (req.heads set): 1 = linked into the
 * chain (no kernel job yet), 0 = submit it now (the chain was submitted
 * first, keeping order), <0 = error. vertex_hazard: the frame's vertex stage
 * may read a buffer an earlier frame of the chain writes. */
static int chain_take(struct bridge *b,struct nord2_gpu_submit_request *req,struct bo *cmd,unsigned vertex_hazard){
    int s=b->chain.slice_now;b->chain.slice_now=-1;
    if(s<0){int e=chain_flush(b,CHAIN_FLUSH_OTHER);return e?e:0;}
    unsigned dep=0,extra=0;
    for(unsigned i=0;i<req->nr_handles;i++){
        if(chain_holds(b,req->handles[i])){if(chain_written(b,req->handles[i]))dep=1;}
        else extra++;
    }
    if(b->chain.frames&&(vertex_hazard||(dep&&b->chain.mode==2))){int e=chain_flush(b,CHAIN_FLUSH_DEP);if(e)return e;dep=0;}
    else if(b->chain.frames&&b->chain.req.nr_handles+extra>32){int e=chain_flush(b,CHAIN_FLUSH_HANDLES);if(e)return e;dep=0;}
    if(!cmd||!cmd->cpu)return BAD;
    uint64_t lo=cmd->va,hi=cmd->va+cmd->size;
    if(req->heads[0]<lo||req->heads[0]+32>hi||req->heads[1]<lo||req->heads[1]+32>hi)return BAD;
    /* JS1: renumber this frame's tiler jobs after the chain's. */
    unsigned base=b->chain.tn,top=base;uint8_t *last=NULL;
    for(uint64_t h=req->heads[0],steps=0;h;steps++) {
        if(h<lo||h+32>hi||steps>N2MB_BATCH_CAPACITY+2)return BAD;
        uint8_t *jh=(uint8_t *)(uintptr_t)(cmd->cpu+(h-lo));uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
        unsigned index=(w4>>16)+base,d1=w5&0xffffu,d2=w5>>16;
        if(index>0xffffu)return BAD;
        if(d1)d1+=base;else if(!steps&&base)d1=base;
        if(d2)d2+=base;
        w4=(w4&0xffffu)|(index<<16);w5=(d1&0xffffu)|((d2&0xffffu)<<16);memcpy(jh+16,&w4,4);memcpy(jh+20,&w5,4);
        if(index>top)top=index;last=jh;h=chain_get64(jh+24);
    }
    if(!last)return BAD;
    /* JS0: this frame's fragment job after the chain's last one. */
    uint8_t *fj=(uint8_t *)(uintptr_t)(cmd->cpu+(req->heads[1]-lo));uint32_t w4,w5;memcpy(&w4,fj+16,4);
    unsigned fidx=b->chain.fn+1;
    w4=(w4&0xffffu)|(fidx<<16);if(b->chain.fn)w4|=1u<<8; /* barrier */
    w5=b->chain.fn;memcpy(fj+16,&w4,4);memcpy(fj+20,&w5,4);
    if(b->chain.ttail)chain_put64(b->chain.ttail+24,req->heads[0]);else b->chain.req.heads[0]=req->heads[0];
    if(b->chain.ftail)chain_put64(b->chain.ftail+24,req->heads[1]);else b->chain.req.heads[1]=req->heads[1];
    b->chain.ttail=last;b->chain.tn=top;b->chain.ftail=fj;b->chain.fn=fidx;
    if(!b->chain.frames){b->chain.req.version=req->version;b->chain.req.kind=req->kind;b->chain.code_clock=b->code_protect;}
    for(unsigned i=0;i<req->nr_handles;i++) {
        unsigned n=0;for(;n<b->chain.req.nr_handles;n++)if(b->chain.req.handles[n]==req->handles[i])break;
        if(n==b->chain.req.nr_handles){if(n==32)return BAD;b->chain.req.handles[b->chain.req.nr_handles++]=req->handles[i];}
        if((req->write_mask>>i)&1u)b->chain.req.write_mask|=1u<<n;
    }
    b->chain.used|=1u<<s;b->chain.frames++;if(dep)b->chain.st[2]++;
    if(b->chain.nblocks<N2MB_CHAIN_MAX_FRAMES)b->chain.blocks[b->chain.nblocks++]=cmd;
    return 1;
}
