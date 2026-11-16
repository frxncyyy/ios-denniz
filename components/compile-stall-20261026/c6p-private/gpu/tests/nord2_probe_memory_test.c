#include <assert.h>
#include <stdio.h>
#include "../metal/nord2_probe_memory.h"
static unsigned calls, fail, corrupt;
static int control(uint32_t command,int32_t pid,uint32_t flags,void *buffer,size_t size) {
    calls++;
    assert(pid == 123 && !flags && size == 16);
    assert(command == (calls == 2 ? 7u : 8u));
    if (fail == calls) return -7;
    struct n2_probe_limits *p = buffer;
    if (calls == 1) *p = (struct n2_probe_limits){50,1,50,1};
    if (calls == 2) assert(p->active == 256 && p->inactive == 256 && p->active_attr == 1 && p->inactive_attr == 1);
    if (calls == 3) {
        *p = (struct n2_probe_limits){256,1,256,1};
        if (corrupt == 1) p->active = 50;
        if (corrupt == 2) p->inactive = 50;
        if (corrupt == 3) p->active_attr = 0;
        if (corrupt == 4) p->inactive_attr = 0;
    }
    return 0;
}
int main(void) {
    struct n2_probe_limits before={0},after={0};unsigned stage=99;
    assert(n2_probe_memory_setup(NULL,123,&before,&after,&stage) && !stage && !calls);
    assert(n2_probe_memory_setup(control,0,&before,&after,&stage) && !stage && !calls);
    assert(n2_probe_memory_setup(control,123,&before,&after,&stage) == 0);
    assert(stage == 5 && calls == 3 && before.active == 50 && after.active == 256);
    for (fail=1;fail<=3;fail++) {
        calls=0;
        assert(n2_probe_memory_setup(control,123,&before,&after,&stage) == -7);
        assert(stage == fail && calls == fail);
    }
    fail=0;
    for (corrupt=1;corrupt<=4;corrupt++) {
        calls=0;
        assert(n2_probe_memory_setup(control,123,&before,&after,&stage));
        assert(stage == 4 && calls == 3);
    }
    puts("PASS finite self-process memory policy, readback and failure paths");
}
