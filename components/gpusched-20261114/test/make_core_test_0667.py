#!/usr/bin/env python3
"""Generates core_test_0667.c from core_test_0666.c: write-combined (N2C_BO_CPU_UNCACHED) buffers.  In the random runs 1 in 3 maps is WC; the
CPU 'writes' visible WC buffers at random (no clean: the write goes past the CPU caches, the model marks its pages stale in the GPU L2), so a
chain that then starts with only INV_SHADER_OTHER and reads them is a violation (the q10jperf4 DATA_INVALID_FAULT in Maps)."""
import os
H = os.path.dirname(os.path.abspath(__file__))
s = open(os.path.join(H, 'core_test_0666.c')).read()
def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)
rep("/* 0666 = the 0664 model test", "/* 0667 = the 0666 model test + write-combined buffers (make_core_test_0667.py).  0666 = the 0664 model test")
rep(" bool adopted=false;enum n2c_error e=n2c_bo_map(&core,c,p,n,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC,owner,n*4096ull,h,va,&adopted);",
    " bool adopted=false;enum n2c_error e=n2c_bo_map(&core,c,p,n,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC|(g_wc&&rnd(3)==0?N2C_BO_CPU_UNCACHED:0u),owner,n*4096ull,h,va,&adopted);")
rep("static uint64_t rs = 1;", "static uint64_t rs = 1;\nstatic bool g_wc;")
# CPU writes through the WC mapping of a visible buffer
rep("  if(g_l2&&q->open&&q->nb&&rnd(40)==0){", """  if(g_wc&&q->open&&q->nb&&rnd(8)==0){unsigned i=rnd(q->nb);const struct n2c_bo*b=n2c_bo_lookup(&core,q->c,q->h[i]);
   if(b&&b->cpu_visible&&(b->flags&N2C_BO_CPU_UNCACHED))for(unsigned pg=0;pg<b->nr_pages;pg++)lset(lstale,lsgen,b->pages[pg].cpu);}   /* WC write: no clean, L2 stale */
  if(g_l2&&q->open&&q->nb&&rnd(40)==0){""")
rep(""" g_pg=true;
 for(unsigned i=0;i<2;i++)t_random(steps,seeds[i]^0x666,true,true);""", """ g_pg=true;
 for(unsigned i=0;i<2;i++)t_random(steps,seeds[i]^0x666,true,true);
 g_wc=true;
 for(unsigned i=0;i<3;i++)t_random(steps,seeds[i]^0x667,true,true);
 printf("0667 random with write-combined buffers: done\\n");
 g_wc=false;""")
rep(""" t_pg();""", """ t_pg();
 t_wc();""")
rep("""\n#define P 12\n#define PB 4""", r"""
/* the q10jperf4 Maps case: a recycled WC descriptor buffer rewritten by the CPU between two jobs needs the L2 invalidate */
static void t_wc(void){g_where=__func__;
 init(true);core.l2pol=true;CHECK(n2c_set_track_level(&core,N2C_TRACK_EXPLICIT));unsigned a=context();
 struct n2c_page*p=calloc(1,sizeof(*p));void*owner=calloc(1,4096);p[0].pa=0x90000000ull;p[0].cpu=owner;bool adopted=false;uint32_t h;uint64_t v;
 CHECK(n2c_bo_map(&core,a,p,1,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_CPU_UNCACHED,owner,4096,&h,&v,&adopted)==N2C_OK);free(p);if(!adopted)free(owner);
 CHECK(n2c_bo_cpu_visible(&core,a,h)==N2C_OK);
 uint32_t hh[1]={h};uint64_t vv[1]={v};
 uint64_t j1=psub(a,N2C_JOB_COMPUTE,1,hh,vv,1,0,true);gpu_step(1,false);poll();CHECK(st(j1)==N2C_JOB_DONE);
 lset(lstale,lsgen,owner);                                  /* the CPU writes new descriptors through the WC mapping */
 uint64_t j2=psub(a,N2C_JOB_COMPUTE,1,hh,vv,1,0,true);CHECK(((m.sl[1].cur.cfg>>8)&3u)==3u);gpu_step(1,false);poll();CHECK(st(j2)==N2C_JOB_DONE);inv();
}

#define P 12
#define PB 4""")
rep("CORE_0666_HOST_TESTS_OK", "CORE_0667_HOST_TESTS_OK")
open(os.path.join(H, 'core_test_0667.c'), 'w').write(s)
print('core_test_0667.c written')
