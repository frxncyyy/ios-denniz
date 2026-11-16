#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hash_vectors.h"
#include "native_codecheck_diag.h"
#include "tegu_gpu_codecheck_map.h"
#include "tegu_gpu_codecheck_readback.h"

static void test_readback(void)
{
    uint32_t page[1024];uint8_t expected[256];
    for(unsigned i=0;i<1024;i++)page[i]=TEGU_CODECHECK_READBACK_SENTINEL;
    for(unsigned i=0;i<256;i++)expected[i]=(uint8_t)(i*29u+7u);
    memcpy(page,expected,256);page[64]=TEGU_CODECHECK_READBACK_DONE;
    struct tegu_codecheck_readback_result r={0};
    assert(!tegu_codecheck_readback_verify(page,sizeof(page),expected,256,&r));
    assert(r.marker_ok&&!r.mismatches&&!r.guard_errors&&r.first_diff==256);
    ((uint8_t *)page)[65]^=1;
    assert(!tegu_codecheck_readback_verify(page,sizeof(page),expected,256,&r));
    assert(r.marker_ok&&r.mismatches==1&&r.first_diff==65&&!r.guard_errors);
    ((uint8_t *)page)[65]^=1;page[1023]^=1;
    assert(!tegu_codecheck_readback_verify(page,sizeof(page),expected,256,&r));
    assert(r.marker_ok&&!r.mismatches&&r.guard_errors==1);
    page[64]=0;
    assert(tegu_codecheck_readback_verify(page,sizeof(page),(const void *)1,256,&r)==-2);
    assert(!r.marker_ok&&!r.mismatches&&!r.guard_errors); /* Payload never read. */
    assert(tegu_codecheck_readback_verify(page,4095,expected,256,&r)==-1);
    assert(tegu_codecheck_readback_verify(page,4096,expected,255,&r)==-1);
    uint64_t commands[TEGU_CODECHECK_READBACK_WORDS];
    assert(tegu_codecheck_readback_prefix(NULL)==0);
    assert(tegu_codecheck_readback_prefix(commands)==34);
    for(unsigned i=0;i<34;i++)printf("PREFIX %016llx\n",(unsigned long long)commands[i]);
}

static void test_hash_and_capture(void)
{
    enum { PKG=28416, STAGED=0x70000, VS_PKG=1664, FS_PKG=2816,
           VS_N=1152, FS_N=25600, VS_ST=0x2000, FS_ST=0x30000 };
    uint8_t package[PKG],staged[STAGED];
    for(size_t i=0;i<sizeof(package);i++)package[i]=(uint8_t)(i*37u+11u);
    memset(staged,0xa5,sizeof(staged));
    memcpy(staged+VS_ST,package+VS_PKG,VS_N);
    memcpy(staged+FS_ST,package+FS_PKG,FS_N);
    char digest[65];
    assert(tegu_codecheck_sha256(package,sizeof(package),digest)==0);
    assert(strcmp(digest,EXPECTED_PACKAGE_SHA)==0);
    for(size_t i=0;i<sizeof(hash_boundary_vectors)/sizeof(hash_boundary_vectors[0]);i++) {
        uint8_t bytes[TEGU_NATIVE_CODECHECK_MAX_BYTES];
        for(size_t j=0;j<hash_boundary_vectors[i].size;j++)bytes[j]=(uint8_t)(j*37u+11u);
        assert(tegu_codecheck_sha256(bytes,hash_boundary_vectors[i].size,digest)==0);
        assert(strcmp(digest,hash_boundary_vectors[i].sha256)==0);
    }
    uint32_t po[2]={VS_PKG,FS_PKG},cb[2]={VS_N,FS_N},so[2]={VS_ST,FS_ST};
    struct tegu_codecheck_result r={0};
    assert(tegu_codecheck_capture(package,sizeof(package),po,cb,staged,sizeof(staged),so,&r)==0);
    assert(r.valid&&r.vs_equal&&r.fs_equal&&r.vs_first_diff==VS_N&&r.fs_first_diff==FS_N);
    assert(strcmp(r.package_sha256,EXPECTED_PACKAGE_SHA)==0);
    assert(strcmp(r.package_vs_sha256,EXPECTED_VS_SHA)==0);
    assert(strcmp(r.package_fs_sha256,EXPECTED_FS_SHA)==0);
    assert(strcmp(r.package_vs_sha256,r.staged_vs_sha256)==0);
    assert(strcmp(r.package_fs_sha256,r.staged_fs_sha256)==0);
    staged[FS_ST+0x1a80]^=1;
    assert(tegu_codecheck_capture(package,sizeof(package),po,cb,staged,sizeof(staged),so,&r)==0);
    assert(r.valid&&r.vs_equal&&!r.fs_equal&&r.fs_first_diff==0x1a80);
    staged[FS_ST+0x1a80]^=1;
    package[FS_PKG+0x1a80]^=1;
    assert(tegu_codecheck_capture(package,sizeof(package),po,cb,staged,sizeof(staged),so,&r)==0);
    assert(r.valid&&r.vs_equal&&!r.fs_equal&&r.fs_first_diff==0x1a80);
    package[FS_PKG+0x1a80]^=1;

    struct tegu_codecheck_result before;memset(&before,0x5a,sizeof(before));r=before;
    uint32_t oob[2]={VS_PKG,PKG-2},sizes[2]={VS_N,FS_N};
    assert(tegu_codecheck_capture(package,sizeof(package),oob,sizes,staged,sizeof(staged),so,&r)==-2);
    assert(memcmp(&r,&before,sizeof(r))==0);
    assert(tegu_codecheck_capture(package,sizeof(package),po,sizes,staged,sizeof(staged),
                                  (uint32_t[2]){VS_ST,STAGED-1},&r)==-2);
    assert(tegu_codecheck_sha256(package,0,digest)==-1);
    assert(tegu_codecheck_sha256(package,TEGU_NATIVE_CODECHECK_MAX_BYTES+1,digest)==-1);
}

static void test_page_roles(void)
{
    struct fs_page pages[FS_V7_SCRATCH_PAGES]={0};
    volatile uint64_t entries[FS_L3_ENTRIES]={0};
    struct fs_pt_page l3[1]={0}; l3[0].entries=entries;
    struct fs_request req={.scratch_exec_mask=UINT64_C(0x007f00000000400c),
       .scratch=pages,.scratch_count=FS_V7_SCRATCH_PAGES,.l3=l3,.l3_count=1};
    for(unsigned i=0;i<FS_V7_SCRATCH_PAGES;i++) {
        pages[i].pa=UINT64_C(0x10000000)+(uint64_t)i*FS_PAGE_SIZE;
        entries[i]=fs_leaf_value(&req,i);
    }
    for(unsigned i=48;i<55;i++) {
        struct tegu_codecheck_leaf leaf;
        assert(tegu_codecheck_fs_leaf(&req,i,&leaf)==0);
        assert(leaf.va==FS_APERTURE_VA+(uint64_t)i*FS_PAGE_SIZE);
        assert(leaf.pa==pages[i].pa); /* distinct PA per index catches ordinal addressing */
        assert(leaf.expected==entries[i]&&leaf.actual==entries[i]&&leaf.match&&leaf.role==1);
    }
    entries[50]^=FS_PTE_RW_NX^FS_PTE_RO_X;
    struct tegu_codecheck_leaf leaf;
    assert(tegu_codecheck_fs_leaf(&req,50,&leaf)==0&&!leaf.match);
    req.scratch_exec_mask&=~(UINT64_C(1)<<50);
    assert(tegu_codecheck_fs_leaf(&req,50,&leaf)==0&&!leaf.role);
    assert(tegu_codecheck_fs_leaf(&req,112,&leaf)==-1);
    for(unsigned i=64;i<FS_V7_SCRATCH_PAGES;i++) {
        assert(tegu_codecheck_fs_leaf(&req,i,&leaf)==0);
        assert(leaf.role==0&&leaf.expected==fs_leaf_value(&req,i));
    }
    assert(tegu_codecheck_fs_leaf(&req,111,&leaf)==0&&leaf.role==0);
    assert(tegu_codecheck_fs_leaf(NULL,50,&leaf)==-1);
    assert(tegu_codecheck_fs_leaf(&req,50,NULL)==-1);
    struct fs_request bad={0};
    assert(tegu_codecheck_fs_leaf(&bad,0,&leaf)==-1);
}

int main(void)
{
    test_hash_and_capture();test_page_roles();test_readback();
    puts("PASS native staging hashes, corruption bounds, page roles and marker-qualified readback");return 0;
}
