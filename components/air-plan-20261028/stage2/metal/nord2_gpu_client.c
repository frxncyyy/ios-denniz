/*
 * Nord 2 private GPU transport diagnostic client (source; build on iOS/macOS
 * with IOKit).  It never writes arbitrary MMIO; it only calls the reviewed
 * selectors.  Build with the iOS 16 SDK and run only with explicit approval.
 */
#include <stdio.h>
#include <string.h>
#include <IOKit/IOKitLib.h>

struct n2_status {
        unsigned abi_version, enabled, powered, arch_ok;
        unsigned gpu_id, l2_features, core_features, tiler_features;
        unsigned mem_features, mmu_features, js_present;
        unsigned shader_present_lo, shader_present_hi;
        unsigned tiler_present_lo, tiler_present_hi;
        unsigned jobs_completed, jobs_faulted, jobs_timed_out;
        unsigned buffers_live, quarantine;
        unsigned mfg_pwr_status, mfg_pwr_status_2nd;
};

static int call(io_connect_t c, unsigned sel, const uint64_t *in,
                unsigned inc, uint64_t *out, uint32_t *outc,
                void *ostruct, size_t *osize)
{
        return IOConnectCallMethod(c, sel, in, inc, 0, 0, out, outc,
                                   ostruct, osize);
}

int main(int argc, char **argv)
{
        io_service_t service = IOServiceGetMatchingService(
                kIOMasterPortDefault, IOServiceMatching("Nord2GPU"));
        if (!service) {
                fprintf(stderr, "Nord2GPU service not found (GPU disabled)\n");
                return 1;
        }
        io_connect_t conn = 0;
        kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &conn);
        IOObjectRelease(service);
        if (kr) {
                fprintf(stderr, "IOServiceOpen failed 0x%x\n", kr);
                return 1;
        }
        struct n2_status st;
        memset(&st, 0, sizeof(st));
        uint32_t n = 1;
        size_t size = sizeof(st);
        uint64_t out[1] = {0};
        kr = call(conn, 0, 0, 0, out, &n, &st, &size);
        if (kr) {
                fprintf(stderr, "STATUS failed 0x%x\n", kr);
                IOServiceClose(conn);
                return 1;
        }
        printf("abi=%u enabled=%u powered=%u arch_ok=%u gpu_id=0x%08x\n",
               st.abi_version, st.enabled, st.powered, st.arch_ok, st.gpu_id);
        printf("features l2=0x%08x core=0x%08x tiler=0x%08x mem=0x%08x "
               "mmu=0x%08x js=0x%08x\n", st.l2_features, st.core_features,
               st.tiler_features, st.mem_features, st.mmu_features,
               st.js_present);
        printf("shader_present=0x%08x/%08x tiler=0x%08x/%08x "
               "mfg_pwr=0x%08x/%08x\n", st.shader_present_lo,
               st.shader_present_hi, st.tiler_present_lo,
               st.tiler_present_hi, st.mfg_pwr_status,
               st.mfg_pwr_status_2nd);
        printf("jobs done=%u fault=%u timeout=%u buffers=%u quarantine=%u\n",
               st.jobs_completed, st.jobs_faulted, st.jobs_timed_out,
               st.buffers_live, st.quarantine);
        if (argc > 1 && !strcmp(argv[1], "--probe")) {
                n = 0;
                kr = call(conn, 6, 0, 0, out, &n, 0, 0);
                printf("PROBE %s (0x%x)\n", kr ? "FAIL" : "PASS", kr);
        }
        IOServiceClose(conn);
        return 0;
}
