#include "fullscreen_map.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int lock_ok(void *p,uint64_t b,unsigned s){(void)p;(void)b;(void)s;return 0;}
static int write_ok(void *p,volatile uint64_t *d,uint64_t v){(void)p;*d=v;return 0;}
static int sync_ok(void *p,volatile uint64_t *d,size_t n){(void)p;(void)d;(void)n;return 0;}
static int unlock_ok(void *p){(void)p;return 0;}
static int not_reserved(void *p,uint64_t pa){(void)p;(void)pa;return 0;}

static void run(uint64_t mask)
{
    struct fs_page scratch[FS_V7_SCRATCH_PAGES],target[1],resource[1];
    struct fs_pt_page l3[1];
    uint64_t table[FS_L3_ENTRIES]={0},l2[512]={0},hash[FS_HASH_CAPACITY]={0};
    uint8_t roles[FS_HASH_CAPACITY]={0};
    const uint64_t owner=7,pa0=UINT64_C(0x883000000);
    for(unsigned i=0;i<FS_V7_SCRATCH_PAGES;i++)
        scratch[i]=(struct fs_page){pa0+(uint64_t)i*FS_PAGE_SIZE,owner};
    target[0]=(struct fs_page){pa0+(uint64_t)FS_V7_SCRATCH_PAGES*FS_PAGE_SIZE,owner};
    resource[0]=(struct fs_page){pa0+(uint64_t)(FS_V7_SCRATCH_PAGES+1)*FS_PAGE_SIZE,owner};
    l3[0]=(struct fs_pt_page){{pa0+(uint64_t)(FS_V7_SCRATCH_PAGES+2)*FS_PAGE_SIZE,owner},table};
    struct fs_request r={
        .owner=owner,.scratch_exec_mask=mask,.pa_min=UINT64_C(0x882000000),
        .pa_limit=UINT64_C(0x8c0000000),.target_bytes=FS_PAGE_SIZE,
        .scratch=scratch,.scratch_count=FS_V7_SCRATCH_PAGES,
        .target=target,.target_count=1,.resources=resource,.resource_count=1,
        .l3=l3,.l3_count=1,
        .l2_entries=l2,.l2_page_pa=UINT64_C(0x880102000),
        .l2_pa_min=UINT64_C(0x880102000),.l2_pa_limit=UINT64_C(0x880103000),
        .validation_hash=hash,.validation_hash_count=FS_HASH_CAPACITY,
        .validation_roles=roles,.validation_roles_count=FS_HASH_CAPACITY,
        .is_reserved_pa=not_reserved
    };
    struct fs_ops o={NULL,lock_ok,write_ok,sync_ok,unlock_ok};
    struct fs_mapping m={0};
    assert(fs_mapping_init(&m));
    assert(fs_map(&m,&r,&o)==FS_KEEP_LIVE);
    for(unsigned i=0;i<FS_V7_SCRATCH_PAGES;i++) {
        uint64_t want=scratch[i].pa;
        if(i<64 && (mask&(UINT64_C(1)<<i))) want|=FS_PTE_RO_X;
        else want|=FS_PTE_RW_NX;
        assert(table[i]==want);
    }
    assert(table[FS_V7_SCRATCH_PAGES]==(target[0].pa|FS_PTE_RW_NX));
    assert(table[FS_V7_SCRATCH_PAGES+1]==(resource[0].pa|FS_PTE_RO_NX));
    assert(fs_unmap(&m)==FS_RELEASED);
    assert(fs_mapping_init(&m));
    r.scratch_count=FS_V7_SCRATCH_PAGES-1;
    assert(fs_map(&m,&r,&o)==FS_RELEASE_STAGED);
    r.scratch_count=FS_V7_SCRATCH_PAGES;
    r.scratch_exec_mask|=UINT64_C(1)<<55;
    assert(fs_map(&m,&r,&o)==FS_RELEASE_STAGED);
    r.scratch_exec_mask=mask;
    target[0]=scratch[0];
    assert(fs_map(&m,&r,&o)==FS_RELEASE_STAGED);
    target[0].pa=pa0+(uint64_t)FS_V7_SCRATCH_PAGES*FS_PAGE_SIZE;
    l3[0].page.pa=scratch[1].pa;
    assert(fs_map(&m,&r,&o)==FS_RELEASE_STAGED);
    l3[0].page.pa=pa0+(uint64_t)(FS_V7_SCRATCH_PAGES+2)*FS_PAGE_SIZE;
    resource[0]=target[0];
    assert(fs_map(&m,&r,&o)==FS_RELEASE_STAGED);
    resource[0]=scratch[0]; /* writable/executable scratch cannot alias a RO resource */
    assert(fs_map(&m,&r,&o)==FS_RELEASE_STAGED);
}

int main(void)
{
    const uint64_t high=UINT64_C(0x007f000000000000);
    run(high|UINT64_C(0xc));                         /* v7 CLEAR */
    run(high|UINT64_C(0x400c));                      /* v7 LOAD */
    run(UINT64_C(0x000700000000000c));               /* B3 CLEAR */
    struct fs_page oldscratch[FS_SCRATCH_PAGES],oldtarget[1],oldresource[1];
    const uint64_t oldowner=9,oldpa=UINT64_C(0x884000000);
    for(unsigned i=0;i<FS_SCRATCH_PAGES;i++)
        oldscratch[i]=(struct fs_page){oldpa+(uint64_t)i*FS_PAGE_SIZE,oldowner};
    oldtarget[0]=(struct fs_page){oldpa+(uint64_t)FS_SCRATCH_PAGES*FS_PAGE_SIZE,oldowner};
    oldresource[0]=(struct fs_page){oldpa+(uint64_t)(FS_SCRATCH_PAGES+1)*FS_PAGE_SIZE,oldowner};
    struct fs_request old={.scratch_exec_mask=FS_SCRATCH_EXEC_CLEAR,
        .scratch=oldscratch,.scratch_count=FS_SCRATCH_PAGES,.target=oldtarget,
        .target_count=1,.resources=oldresource,.resource_count=1};
    for(unsigned i=0;i<FS_SCRATCH_PAGES;i++)
        assert(fs_leaf_value(&old,i)==(oldscratch[i].pa|
            ((FS_SCRATCH_EXEC_CLEAR&(UINT64_C(1)<<i))?FS_PTE_RO_X:FS_PTE_RW_NX)));
    assert(fs_leaf_value(&old,FS_SCRATCH_PAGES)==(oldtarget[0].pa|FS_PTE_RW_NX));
    assert(fs_leaf_value(&old,FS_SCRATCH_PAGES+1)==(oldresource[0].pa|FS_PTE_RO_NX));
    struct fs_mapping m={0};
    /* The fixed 64-page v6 validation mask remains separately bounded. */
    assert((FS_SCRATCH_EXEC_ALLOWED & ~UINT64_C(0xf00c))==0);
    assert((FS_V7_SCRATCH_EXEC_ALLOWED & ~UINT64_C(0x007f00000000400c))==0);
    assert(fs_mapping_init(&m));
    puts("v7 112-page map + v6 64-page leaf parity: X/RX whitelist, RW/NX data, target/resource ordering, teardown PASS");
}
