#!/usr/bin/env python3
"""Generates core_test_0666.c from core_test_0664.c (step C: 0666 shader-core power gating).  The model powers the cores off only with no chain in
any slot (current or NEXT), never starts a chain with the cores off, and the random runs gate after 2 ms idle; targeted: off after the idle time,
on before the next start, a failed power-down switches gating off, a reset powers up."""
import os
H = os.path.dirname(os.path.abspath(__file__))
s = open(os.path.join(H, 'core_test_0664.c')).read()
def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)
rep("/* 0664 = the 0661 model test", "/* 0666 = the 0664 model test + shader power gating (make_core_test_0666.py).  0664 = the 0661 model test")
rep("enum n2hw_error n2hw_power_on_cores(struct n2hw*h,uint64_t mask){h->shaders_on=mask;h->l2_on=true;return N2HW_OK;}",
    """static unsigned pg_offs_model, pg_ons_model; static bool pg_fail_next;
enum n2hw_error n2hw_power_on_cores(struct n2hw*h,uint64_t mask){if(!h->shaders_on)pg_ons_model++;h->shaders_on=mask;h->l2_on=true;return N2HW_OK;}
enum n2hw_error n2hw_power_off_cores(struct n2hw*h){for(unsigned s2=0;s2<16;s2++)CHECK(!m.sl[s2].cur.v&&!m.sl[s2].nxt.v);   /* never under a chain */
 if(pg_fail_next){pg_fail_next=false;return N2HW_E_SHADER_TIMEOUT;}h->shaders_on=0;ldgen++;pg_offs_model++;return N2HW_OK;}""")
rep("enum n2hw_error n2hw_js_start(struct n2hw*h,unsigned s,unsigned as,uint64_t head,uint64_t aff){(void)h;CHECK(head&&aff&&s<16);",
    "enum n2hw_error n2hw_js_start(struct n2hw*h,unsigned s,unsigned as,uint64_t head,uint64_t aff){(void)h;CHECK(head&&aff&&s<16);CHECK(hw.shaders_on&&!(aff&~hw.shaders_on));")
rep("static bool g_pipe, g_prep, g_prio, g_l2;", "static bool g_pipe, g_prep, g_prio, g_l2, g_pg;")
rep("core.l2pol=g_l2;", "core.l2pol=g_l2;core.pg_on=g_pg;core.pg_idle_us=2000;")
rep(""" t_l2();""", """ t_l2();
 t_pg();""")
rep(""" g_l2=true;
 for(unsigned i=0;i<3;i++)t_random(steps,seeds[i]^0x664,true,true);""", """ g_l2=true;
 for(unsigned i=0;i<3;i++)t_random(steps,seeds[i]^0x664,true,true);
 g_pg=true;
 for(unsigned i=0;i<2;i++)t_random(steps,seeds[i]^0x666,true,true);
 printf("0666 random: power-downs %u power-ups %u\\n",pg_offs_model,pg_ons_model);
 g_pg=false;""")
rep("""\n#define P 12\n#define PB 4""", r"""
static void t_pg(void){g_where=__func__;
 init(true);core.pg_on=true;core.pg_idle_us=10000;unsigned a=context();uint32_t h;uint64_t v;CHECK(map_pages(a,1,&h,&v)==N2C_OK);
 uint64_t j=submit(a,h,v,N2C_JOB_COMPUTE);gpu_step(1,false);poll();CHECK(st(j)==N2C_JOB_DONE&&!core.cores_off);
 m.time+=5;poll();CHECK(!core.cores_off);m.time+=6;poll();CHECK(core.cores_off&&!hw.shaders_on&&core.prof.pg_offs==1);   /* 11 ms idle */
 j=submit(a,h,v,N2C_JOB_COMPUTE);CHECK(!core.cores_off&&hw.shaders_on&&core.running[1]==j&&core.prof.pg_ons==1);
 gpu_step(1,false);poll();CHECK(st(j)==N2C_JOB_DONE);inv();
 /* queued work keeps them on; a failed power-down switches gating off */
 m.time+=11;poll();m.time+=11;poll();CHECK(core.cores_off);j=submit(a,h,v,N2C_JOB_COMPUTE);gpu_step(1,false);poll();
 pg_fail_next=true;m.time+=11;poll();m.time+=11;poll();CHECK(!core.cores_off&&!core.pg_on&&core.prof.pg_fail==1);inv();
}

#define P 12
#define PB 4""")
rep("CORE_0664_HOST_TESTS_OK", "CORE_0666_HOST_TESTS_OK")
open(os.path.join(H, 'core_test_0666.c'), 'w').write(s)
print('core_test_0666.c written')
