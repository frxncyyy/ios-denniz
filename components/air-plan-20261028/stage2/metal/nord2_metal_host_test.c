#include "nord2_metal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char arena[64 * 1024];
static unsigned allocs, frees, submits, waits;
static struct n2m_compute_request seen;

static unsigned scalar(void *ctx, unsigned sel, const uint64_t *args,
                       unsigned argc, uint64_t *out, unsigned *n)
{
        (void)ctx; (void)argc;
        if (sel == N2M_SELECTOR_ALLOC) {
                assert(*n == 3);
                out[0] = ++allocs; out[1] = 0x100000; out[2] = args[0];
                return 0;
        }
        if (sel == N2M_SELECTOR_RELEASE) { frees++; return 0; }
        return 1;
}
static int method(void *ctx, unsigned sel, const uint64_t *args, unsigned argc,
                  const void *in, size_t insz, uint64_t *out, unsigned *n,
                  void *ostruct, unsigned *osize)
{
        (void)ctx; (void)args; (void)argc; (void)ostruct; (void)osize;
        if (sel == N2M_SELECTOR_SUBMIT) {
                assert(insz == sizeof(seen));
                memcpy(&seen, in, sizeof(seen));
                submits++; out[0] = 9; *n = 1; return 0;
        }
        if (sel == N2M_SELECTOR_WAIT) { waits++; out[0] = 3; *n = 1; return 0; }
        return 1;
}
static int map(void *ctx, unsigned h, uint64_t *addr, uint64_t *size)
{
        (void)ctx; (void)h; *addr = (uint64_t)(uintptr_t)arena; *size = sizeof(arena); return 0;
}
static int unmap(void *ctx, unsigned h, uint64_t addr) { (void)ctx; (void)h; (void)addr; return 0; }

int main(void)
{
        struct n2m_ops ops = {scalar, method, map, unmap};
        struct n2m_device dev;
        assert(n2m_device_init(&dev, &ops, 0));
        struct n2m_buffer binary, data;
        assert(n2m_buffer_new(&dev, 4096, true, &binary));
        assert(n2m_buffer_new(&dev, 4096, false, &data));
        assert(dev.buffers_live == 2 && dev.allocated_bytes == 8192);
        assert(n2m_buffer_free(&dev, &data));
        assert(n2m_buffer_new(&dev, 4096, false, &data));
        struct n2m_pipeline pipe;
        assert(n2m_pipeline_new(&dev, &binary, 0, &pipe));
        assert(!n2m_buffer_free(&dev, &binary)); /* pipeline ref */
        struct n2m_command cmd;
        struct n2m_encoder enc;
        assert(n2m_command_begin(&dev, &cmd));
        assert(n2m_command_encoder(&cmd, &enc));
        assert(!n2m_encoder_dispatch(&enc, 1, 1, 1, 1, 1, 1));
        assert(n2m_encoder_set_pipeline(&enc, &pipe));
        assert(n2m_encoder_set_buffer(&enc, 0, &data, 0));
        assert(n2m_encoder_set_buffer(&enc, 1, &data, 0));
        assert(!n2m_encoder_set_buffer(&enc, 8, &data, 0));
        assert(!n2m_encoder_dispatch(&enc, 0, 1, 1, 1, 1, 1));
        assert(!n2m_encoder_dispatch(&enc, 1, 1, 1, 1024, 1024, 1));
        assert(n2m_encoder_dispatch(&enc, 2, 3, 1, 4, 5, 1));
        assert(!n2m_encoder_dispatch(&enc, 1, 1, 1, 1, 1, 1));
        assert(n2m_encoder_end(&enc));
        assert(n2m_command_commit(&cmd));
        assert(!n2m_command_commit(&cmd));
        assert(n2m_command_wait(&cmd, 1000));
        assert(n2m_command_state(&cmd) == 3);
        assert(submits == 1 && waits == 1);
        assert(seen.version == 1 && seen.shader_handle == binary.handle);
        assert(seen.resource_handle == data.handle);
        assert(seen.fau_handle == data.handle);
        assert(seen.workgroup_size[0] == 4 && seen.workgroup_size[1] == 5);
        assert(seen.workgroup_count[0] == 2 && seen.workgroup_count[1] == 3);
        assert(seen.fau_count == 1);
        n2m_pipeline_free(&pipe);
        assert(n2m_buffer_free(&dev, &binary));
        assert(n2m_buffer_free(&dev, &data));
        assert(dev.buffers_live == 0 && dev.allocated_bytes == 0);
        printf("Nord2Metal C core host test PASS\n");
        return 0;
}
