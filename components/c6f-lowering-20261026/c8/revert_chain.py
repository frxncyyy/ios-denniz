import hashlib
p='metal/nord2_metal_bridge.c'; s=open(p).read()
def rep(old,new,count=1):
    global s
    assert s.count(old)==count,(old[:70],s.count(old)); s=s.replace(old,new)
a=s.index("\n/* ---- Chained submission: API and the visibility probe -------------------- */"); s=s[:a]+"\n"
a=s.index("    /* Chained submission (n2mb_chain_begin/end, NORD2_METAL_CHAIN). `req`"); b=s.index("};\nenum {CHAIN_FLUSH_DEP",a); s=s[:a]+s[b:]
a=s.index("enum {CHAIN_FLUSH_DEP"); b=s.index("    return 0;\n}\n",a)+len("    return 0;\n}\n"); s=s[:a]+s[b:]
rep("""    /* A buffer of a not yet submitted chain: submit the chain first. */
    if(chain_holds(b,o->handle)){int e=chain_flush(b,CHAIN_FLUSH_OTHER);if(e)return e;}
""","")
rep("""    if(b->chain.frames){int e=chain_flush(b,CHAIN_FLUSH_OTHER);if(e)return e;}
    if(!b->pending)return 0;""","""    if(!b->pending)return 0;""")
a=s.index("/* ---- Chained submission (n2mb_chain_begin/end) ---------------------------"); b=s.index("uintptr_t n2mb_open(const struct n2mb_ops *ops) {\n",a); s=s[:a]+s[b:]
rep("""    b->chain.mode=chain_env("NORD2_METAL_CHAIN",0,2,0);b->chain.max=chain_env("NORD2_METAL_CHAIN_FRAMES",2,N2MB_CHAIN_MAX_FRAMES,4);
    b->chain.probe=chain_env("NORD2_METAL_CHAIN_PROBE",0,1,1)?1:2;b->chain.slice_now=-1;
""","")
rep(" /* code used from here on belongs to this job (and an open chain's) */\n b->code_protect=b->chain.frames?b->chain.code_clock:b->code_clock;"," b->code_protect=b->code_clock; /* code used from here on belongs to this job */")
rep("\n b->code_protect=b->chain.frames?b->chain.code_clock:b->code_clock;\n","\n b->code_protect=b->code_clock;\n")
rep(" b->code_protect=b->chain.frames?b->chain.code_clock:UINT64_MAX;\n"," b->code_protect=UINT64_MAX;\n",2)
rep(""" /* Chained submission: a plain frame takes a tiler-heap slice of the chain. */
 uint64_t heap_va=0;
 struct bo *heap_bo=chain_heap(b,!sync&&!d&&!snapshot&&!(opts&&(opts->thread_storage_bytes||opts->thread_storage_reserved)),&heap_va);
 a.command=a.frame=owned[0]->va;a.heap=heap_va;""",""" a.command=a.frame=owned[0]->va;a.heap=b->heap->va;""")
endm="  else{e=jm_submit(b,&req,sync,&job);pooled_job=job;if(e)N2MB_FAIL_AT();}\n }\n"
a=s.index(" if((e=add_ref(&req,heap_bo,1)))N2MB_FAIL_AT();"); b=s.index(endm,a)+len(endm)
s=s[:a]+""" if((e=add_ref(&req,b->heap,1)))N2MB_FAIL_AT();
 geometry_before(b,b->heap,a.heap_bytes);
 uint64_t job=0;e=jm_submit(b,&req,sync,&job);pooled_job=job;if(e)N2MB_FAIL_AT();
"""+s[b:]
rep(""" uint64_t heap_va=0;struct bo *heap_bo=chain_heap(b,!b->trace_batches,&heap_va); /* chained: the frame's slice */
""","")
rep("  a.command=frame+(uint64_t)k*N2R_BLOCK;a.frame=frame;a.heap=heap_va;","  a.command=frame+(uint64_t)k*N2R_BLOCK;a.frame=frame;a.heap=b->heap->va;")
rep("  pa.frame=frame;pa.heap=heap_va;","  pa.frame=frame;pa.heap=b->heap->va;")
rep("pa.command=frame+(uint64_t)count*N2R_BLOCK;pa.frame=frame;pa.heap=heap_va;","pa.command=frame+(uint64_t)count*N2R_BLOCK;pa.frame=frame;pa.heap=b->heap->va;",2)
rep(" if((e=add_ref(&req,heap_bo,1)))N2MB_BATCH_FAIL();"," if((e=add_ref(&req,b->heap,1)))N2MB_BATCH_FAIL();")
endm="  else{e=jm_submit(b,&req,traced,&job);pooled_job=job;if(e)N2MB_BATCH_FAIL();}}\n"
a=s.index(" uint64_t job=0;\n {unsigned hazard=0;"); b=s.index(endm,a)+len(endm)
s=s[:a]+" uint64_t job=0;e=jm_submit(b,&req,traced,&job);pooled_job=job;if(e)N2MB_BATCH_FAIL();\n"+s[b:]
rep("""    /* Selectors other than capabilities/allocation are ordered after an open
     * chain (releases are covered by drop). */
    if(sel>1&&b->chain.frames&&(e=chain_flush(b,CHAIN_FLUSH_OTHER)))goto done;
""","")
rep("""    if(b->chain.frames&&!b->broken&&(e=chain_flush(b,CHAIN_FLUSH_OTHER))){b->ops.unlock(b->ops.opaque);return e;}
    e=UNSUPPORTED;
""","")
rep("""\n if(b->chain.frames)(void)chain_flush(b,CHAIN_FLUSH_OTHER);\n""","\n")
rep("""\n if(b->chain.frames&&!b->broken)(void)chain_flush(b,CHAIN_FLUSH_OTHER);\n""","\n")
rep("""    /* CPU access to a buffer of an open chain: submit the chain first. */
    if(o&&!b->broken&&chain_holds(b,o->handle))(void)chain_flush(b,CHAIN_FLUSH_OTHER);
""","")
a=s.index(" /* Chained (reply id 0): busy until the chain's job id is known. */"); b=s.index(" }\n",s.index("else{(void)chain_flush(b,CHAIN_FLUSH_FULL)",a))+3; s=s[:a]+s[b:]
open(p,'w').write(s)
print(hashlib.sha256(open(p,'rb').read()).hexdigest()[:8],'want e90ae2eb')
