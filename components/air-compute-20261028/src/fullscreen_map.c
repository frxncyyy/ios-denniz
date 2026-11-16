#include "fullscreen_map.h"
#include <string.h>

#define FS_MAX_PA UINT64_C(0x10000000000)
#define FS_L2_COUNT(pages) (((pages) + FS_L3_ENTRIES - 1u) / FS_L3_ENTRIES)

static unsigned pages_for(uint32_t bytes) { return (bytes + FS_PAGE_SIZE - 1u) / FS_PAGE_SIZE; }
static int valid_pa(uint64_t pa) { return pa && !(pa & (FS_PAGE_SIZE - 1u)) && pa < FS_MAX_PA; }
static unsigned l2_slot(unsigned i) { return FS_L2_FIRST + i; }
static uint64_t lock_size(unsigned page_count, uint64_t *base)
{
    uint64_t start = FS_APERTURE_VA;
    uint64_t end = start + (uint64_t)page_count * FS_PAGE_SIZE - 1;
    uint64_t differing = start ^ end;
    unsigned top = 0;
    while (differing) { ++top; differing >>= 1; }
    if (top < 20) top = 20; /* GPU lock minimum used by the existing 1 MiB map. */
    *base = start & ~((UINT64_C(1) << top) - 1);
    return top;
}
enum { FS_ROLE_PROTECTED = 1, FS_ROLE_RESOURCE = 2 };
static int insert_page(struct fs_request const *r, uint64_t pa, uint8_t role)
{
    size_t cap = r->validation_hash_count;
    size_t mask = cap - 1;
    size_t idx = (size_t)((pa >> 12) * UINT64_C(11400714819323198485)) & mask;
    for (size_t n = 0; n < cap; ++n) {
        if (!r->validation_hash[idx]) {
            r->validation_hash[idx] = pa;
            r->validation_roles[idx] = role;
            return 1;
        }
        if (r->validation_hash[idx] == pa) {
            /* Every duplicate must be a read-only resource mapping, and no
             * resource may alias an already inserted protected page. */
            if (role == FS_ROLE_RESOURCE &&
                r->validation_roles[idx] == FS_ROLE_RESOURCE) return 2;
            return 0;
        }
        idx = (idx + 1) & mask;
    }
    return 0;
}
static int validate_pages(struct fs_request const *r)
{
    if (!r || !r->owner || !r->l2_entries || !valid_pa(r->l2_page_pa) || !r->l3 || !r->scratch || !r->target || !r->validation_hash || !r->is_reserved_pa ||
        r->pa_min==0 || r->pa_min>=r->pa_limit || r->pa_limit>FS_MAX_PA ||
        !r->validation_roles || r->validation_roles_count != FS_HASH_CAPACITY ||
        r->l2_pa_min==0 || r->l2_pa_min>=r->l2_pa_limit ||
        r->l2_pa_limit>FS_MAX_PA ||
        (r->scratch_count == FS_SCRATCH_PAGES &&
         (r->scratch_exec_mask & ~FS_SCRATCH_EXEC_ALLOWED)) ||
        (r->scratch_count == FS_V7_SCRATCH_PAGES &&
         (r->scratch_exec_mask & ~FS_V7_SCRATCH_EXEC_ALLOWED)) ||
        (r->scratch_count != FS_SCRATCH_PAGES &&
         r->scratch_count != FS_V7_SCRATCH_PAGES) ||
        !r->target_bytes || r->target_bytes > FS_MAX_TARGET_PAGES * FS_PAGE_SIZE ||
        r->target_count != pages_for(r->target_bytes) || r->target_count > FS_MAX_TARGET_PAGES ||
        r->resource_count > FS_MAX_RESOURCE_PAGES || (r->resource_count && !r->resources) ||
        (r->heap_count != 0 && r->heap_count != FS_HEAP_PAGES) ||
        (r->heap_count && !r->heap)) return 0;
    unsigned leaves = (unsigned)(r->scratch_count + r->target_count + r->resource_count + r->heap_count);
    unsigned l3_count = FS_L2_COUNT(leaves);
    if (!l3_count || l3_count > FS_MAX_L3_PAGES || r->l3_count != l3_count || FS_L2_FIRST + l3_count > 512 ||
        r->validation_hash_count != FS_HASH_CAPACITY) return 0;
    memset(r->validation_hash, 0, FS_HASH_CAPACITY * sizeof(uint64_t));
    memset(r->validation_roles, 0, FS_HASH_CAPACITY * sizeof(uint8_t));
    if ((r->l2_pa_min & (FS_PAGE_SIZE - 1u)) ||
        (r->l2_pa_limit & (FS_PAGE_SIZE - 1u)) ||
        r->l2_pa_limit - r->l2_pa_min < FS_PAGE_SIZE ||
        r->l2_page_pa < r->l2_pa_min ||
        r->l2_page_pa > r->l2_pa_limit - FS_PAGE_SIZE ||
        !insert_page(r,r->l2_page_pa,FS_ROLE_PROTECTED)) return 0;
    /* Insert all pages whose aliases are forbidden before considering any
     * resource. That lets the same PA appear at multiple RO resource VAs. */
    for (size_t i=0; i<r->scratch_count+r->target_count; ++i) {
        const struct fs_page *p = i < r->scratch_count ? &r->scratch[i] :
            &r->target[i-r->scratch_count];
        if (p->owner != r->owner || !valid_pa(p->pa) || p->pa<r->pa_min ||
            p->pa>=r->pa_limit || insert_page(r,p->pa,FS_ROLE_PROTECTED)!=1 ||
            r->is_reserved_pa(r->reserved_ctx,p->pa)) return 0;
    }
    for (size_t i=0; i<r->l3_count; ++i) {
        const struct fs_page *p=&r->l3[i].page;
        if (!r->l3[i].entries || p->owner != r->owner || !valid_pa(p->pa) || p->pa<r->pa_min || p->pa>=r->pa_limit ||
            insert_page(r,p->pa,FS_ROLE_PROTECTED)!=1 ||
            r->is_reserved_pa(r->reserved_ctx,p->pa)) return 0;
        for (unsigned j=0;j<FS_L3_ENTRIES;j++) if (r->l3[i].entries[j] != 0) return 0;
    }
    for (size_t i=0; i<r->heap_count; ++i) {
        const struct fs_page *p=&r->heap[i];
        if (p->owner != r->owner || !valid_pa(p->pa) || p->pa<r->pa_min ||
            p->pa>=r->pa_limit || r->is_reserved_pa(r->reserved_ctx,p->pa) ||
            insert_page(r,p->pa,FS_ROLE_PROTECTED)!=1) return 0;
    }
    for (size_t i=0; i<r->resource_count; ++i) {
        const struct fs_page *p=&r->resources[i];
        if (p->owner != r->owner || !valid_pa(p->pa) || p->pa<r->pa_min ||
            p->pa>=r->pa_limit || r->is_reserved_pa(r->reserved_ctx,p->pa) ||
            insert_page(r,p->pa,FS_ROLE_RESOURCE)==0) return 0;
    }
    for (unsigned i=0; i<l3_count; ++i) if (r->l2_entries[l2_slot(i)] != 0) return 0;
    return 1;
}
uint64_t fs_leaf_value(const struct fs_request *r, unsigned index)
{
    const struct fs_page *p; uint64_t attr;
    if (index < r->scratch_count) {
        p=&r->scratch[index];
        attr=(index<64 && (r->scratch_exec_mask & (UINT64_C(1)<<index)))?
            FS_PTE_RO_X:FS_PTE_RW_NX;
    } else if (index < r->scratch_count+r->target_count) {
        p=&r->target[index-r->scratch_count]; attr=FS_PTE_RW_NX;
    } else if (index < r->scratch_count+r->target_count+r->resource_count) {
        p=&r->resources[index-r->scratch_count-r->target_count]; attr=FS_PTE_RO_NX;
    } else {
        p=&r->heap[index-r->scratch_count-r->target_count-r->resource_count]; attr=FS_PTE_RW_NX;
    }
    return p->pa | attr;
}
static int write_entry(const struct fs_ops *o, volatile uint64_t *p, uint64_t v)
{ return o->write_entry(o->ctx,p,v); }
static enum fs_disposition rollback_links(struct fs_mapping *m)
{
    unsigned failed=0;
    for (unsigned i=m->linked_l3; i>0; --i) {
        unsigned slot=l2_slot(i-1);
        if (write_entry(&m->ops,&m->req.l2_entries[slot],0)) failed=1;
    }
    if (failed) return FS_QUARANTINE;
    if (m->ops.flush_unlock(m->ops.ctx)) return FS_QUARANTINE;
    m->linked_l3=0;
    return FS_RELEASE_STAGED;
}
int fs_mapping_init(struct fs_mapping *m)
{
    if (!m) return 0;
    /* Caller provides zeroed storage initially. Never reset live/quarantined state. */
    if (m->magic==FS_MAPPING_MAGIC && (m->active || m->disposition==FS_QUARANTINE)) return 0;
    memset(m,0,sizeof(*m)); m->magic=FS_MAPPING_MAGIC; m->disposition=FS_RELEASE_STAGED; return 1;
}
enum fs_disposition fs_map(struct fs_mapping *m,const struct fs_request *r,const struct fs_ops *o)
{
    if (!m) return FS_RELEASE_STAGED;
    if (m->magic!=FS_MAPPING_MAGIC) return FS_RELEASE_STAGED;
    if (m->active) return FS_KEEP_LIVE;
    if (m->disposition==FS_QUARANTINE) return FS_QUARANTINE;
    if (!o || !o->lock_begin || !o->write_entry || !o->sync_table || !o->flush_unlock || !validate_pages(r)) return FS_RELEASE_STAGED;
    m->req=*r; m->ops=*o;
    m->leaf_count=(unsigned)(r->scratch_count+r->target_count+r->resource_count+r->heap_count);
    m->lock_size_log2=(unsigned)lock_size(m->leaf_count,&m->lock_base);
    if (m->lock_size_log2>48) return m->disposition=FS_RELEASE_STAGED;
    /* Private L3 construction occurs before the level-2 entries can expose it. */
    for (unsigned i=0;i<m->leaf_count;i++) {
        unsigned ti=i/FS_L3_ENTRIES, slot=i%FS_L3_ENTRIES;
        if (write_entry(&m->ops,&r->l3[ti].entries[slot],fs_leaf_value(r,i))) {
            for (size_t t=0;t<r->l3_count;t++) for (unsigned j=0;j<FS_L3_ENTRIES;j++) r->l3[t].entries[j]=0;
            return m->disposition=FS_RELEASE_STAGED;
        }
    }
    for (size_t t=0;t<r->l3_count;t++) {
        if (o->sync_table(o->ctx,r->l3[t].entries,FS_PAGE_SIZE)) {
            for (size_t k=0;k<r->l3_count;k++) for (unsigned j=0;j<FS_L3_ENTRIES;j++) r->l3[k].entries[j]=0;
            return m->disposition=FS_RELEASE_STAGED;
        }
    }
    if (o->lock_begin(o->ctx,m->lock_base,m->lock_size_log2)) {
        for (size_t t=0;t<r->l3_count;t++) for (unsigned j=0;j<FS_L3_ENTRIES;j++) r->l3[t].entries[j]=0;
        return m->disposition=FS_RELEASE_STAGED;
    }
    for (unsigned i=0;i<r->l3_count;i++) {
        uint64_t desc=r->l3[i].page.pa|FS_PTE_L3;
        if (write_entry(o,&r->l2_entries[l2_slot(i)],desc)) {
            enum fs_disposition d=rollback_links(m);
            if (d==FS_QUARANTINE) return m->disposition=d;
            for (size_t t=0;t<r->l3_count;t++) for (unsigned j=0;j<FS_L3_ENTRIES;j++) r->l3[t].entries[j]=0;
            return m->disposition=FS_RELEASE_STAGED;
        }
        m->linked_l3++;
    }
    if (o->flush_unlock(o->ctx)) return m->disposition=FS_QUARANTINE;
    m->active=1; return m->disposition=FS_KEEP_LIVE;
}
enum fs_disposition fs_unmap(struct fs_mapping *m)
{
    if (!m || m->magic!=FS_MAPPING_MAGIC || !m->active || m->disposition!=FS_KEEP_LIVE) return FS_QUARANTINE;
    if (m->ops.lock_begin(m->ops.ctx,m->lock_base,m->lock_size_log2)) return m->disposition=FS_QUARANTINE;
    for (unsigned i=0;i<m->req.l3_count;i++) {
        uint64_t expected=m->req.l3[i].page.pa|FS_PTE_L3;
        if (m->req.l2_entries[l2_slot(i)]!=expected) {
            (void)m->ops.flush_unlock(m->ops.ctx);
            return m->disposition=FS_QUARANTINE;
        }
    }
    for (unsigned i=0;i<m->req.l3_count*FS_L3_ENTRIES;i++) {
        unsigned ti=i/FS_L3_ENTRIES, slot=i%FS_L3_ENTRIES;
        uint64_t expected=i<m->leaf_count?fs_leaf_value(&m->req,i):0;
        if (m->req.l3[ti].entries[slot]!=expected) {
            (void)m->ops.flush_unlock(m->ops.ctx);
            return m->disposition=FS_QUARANTINE;
        }
    }
    m->linked_l3=0;
    for (unsigned i=0;i<m->req.l3_count;i++) {
        if (write_entry(&m->ops,&m->req.l2_entries[l2_slot(i)],0)) {
            /* Re-link every removed entry while the range remains locked. */
            int bad=0;
            for (unsigned j=0;j<i;j++) if(write_entry(&m->ops,&m->req.l2_entries[l2_slot(j)],m->req.l3[j].page.pa|FS_PTE_L3)) bad=1;
            if (bad || m->ops.flush_unlock(m->ops.ctx)) return m->disposition=FS_QUARANTINE;
            return m->disposition=FS_KEEP_LIVE;
        }
        m->linked_l3++;
    }
    if (m->ops.flush_unlock(m->ops.ctx)) return m->disposition=FS_QUARANTINE;
    for (size_t t=0;t<m->req.l3_count;t++) for (unsigned j=0;j<FS_L3_ENTRIES;j++) m->req.l3[t].entries[j]=0;
    m->active=0; return m->disposition=FS_RELEASED;
}
