/* Native test of private TeguGPU ABI v1. No SDK headers or GPU claims.
 * IOKit signatures checked against apple-oss-distributions/IOKitUser/IOKitLib.h.
 * Normal disposal is unmap -> release handle -> close connection. XNU's
 * IOConnectUnmapMemory64 calls clientMemoryForType again, so needs a live handle.
 */
typedef unsigned u32;
typedef unsigned long long u64;
typedef unsigned long usize;
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern long write(int, const void *, usize);
#define MARK(s) ((void)write(2,"TEGU_TRANSPORT " s "\n",sizeof("TEGU_TRANSPORT " s "\n")-1))
#define CHECK(c, label) do { if (!(c)) { MARK("FAIL " label); goto cleanup; } } while (0)

int tegu_transport_probe(void)
{
    MARK("entered");
    void *io=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",2);
    void *sys=dlopen("/usr/lib/libSystem.B.dylib",2);
    if (!io || !sys) { MARK("load_failed"); return 1; }
    void *(*matching)(const char *)=dlsym(io,"IOServiceMatching");
    u32 (*getService)(u32,void *)=dlsym(io,"IOServiceGetMatchingService");
    int (*openService)(u32,u32,u32,u32 *)=dlsym(io,"IOServiceOpen");
    int (*closeService)(u32)=dlsym(io,"IOServiceClose");
    int (*releaseObject)(u32)=dlsym(io,"IOObjectRelease");
    int (*call)(u32,u32,const u64 *,u32,u64 *,u32 *)=dlsym(io,"IOConnectCallScalarMethod");
    int (*map)(u32,u32,u32,u64 *,u64 *,u32)=dlsym(io,"IOConnectMapMemory64");
    int (*unmap)(u32,u32,u32,u64)=dlsym(io,"IOConnectUnmapMemory64");
    const u32 *task=dlsym(sys,"mach_task_self_");
    if (!matching || !getService || !openService || !closeService ||
        !releaseObject || !call || !map || !unmap || !task) {
        MARK("missing_symbol"); return 1;
    }
    void *dict=matching("TeguGPU");
    if (!dict) { MARK("matching_failed"); return 1; }
    u32 service=getService(0,dict); /* consumes matching dictionary */
    if (!service) { MARK("no_service"); return 2; }
    u32 connection=0,peer=0,count=4;
    u64 info[4]={0},result[2]={0},request=65537,handle=0,address=0,size=0;
    u64 peerAddress=0,peerSize=0;
    int mapped=0,peerMapped=0,handleLive=0,success=0;
    volatile u32 *words=0;
    CHECK(openService(service,*task,0,&connection)==0,"open");
    CHECK(openService(service,*task,0,&peer)==0,"peer_open");
    CHECK(call(connection,0,0,0,info,&count)==0 && count==4,"info");
    /* Capability flags are additive within ABI v1; exercise only known bits. */
    CHECK(info[0]==1 && (info[1]&1) && info[2]>=request && info[3]>0,"abi");
    MARK("connected_v1");
    count=2;
    CHECK(call(connection,1,&request,1,result,&count)==0,"allocate");
    handle=result[0]; handleLive=1;
    CHECK(count==2 && handle && handle<=0xffffffffULL && result[1]>=request && result[1]<=info[2],"allocation_reply");
    CHECK(map(connection,(u32)handle,*task,&address,&size,1)==0,"map");
    mapped=1;
    CHECK(address && size==result[1] && !(size&3),"mapping_reply");
    words=(volatile u32 *)(usize)address;
    for (u64 i=0;i<size/4;i++) CHECK(words[i]==0,"zeroing");
    for (u64 i=0;i<size/4;i++) words[i]=0x54454755u^(u32)i;
    for (u64 i=0;i<size/4;i++) CHECK(words[i]==(0x54454755u^(u32)i),"cpu_readback");
    peerMapped=map(peer,(u32)handle,*task,&peerAddress,&peerSize,1)==0;
    CHECK(!peerMapped,"cross_client_handle_visible");
    CHECK(unmap(connection,(u32)handle,*task,address)==0,"unmap");
    mapped=0; address=0; size=0;
    CHECK(map(connection,(u32)handle,*task,&address,&size,1)==0,"remap");
    mapped=1;
    CHECK(address && size==result[1],"remap_size");
    words=(volatile u32 *)(usize)address;
    for (u64 i=0;i<size/4;i++) CHECK(words[i]==(0x54454755u^(u32)i),"remap_contents");
    u64 completed=0;
    if (info[1]&2) {
        MARK("gpu_store_batch_begin");
        for (unsigned i=0;i<300;i++) {
            u64 args[3]={handle,(i%256)*4,0x47500000u+i},reply[2]={0};
            words[i%256]=~(u32)args[2];
            count=2;
            CHECK(call(connection,3,args,3,reply,&count)==0,"gpu_store_call");
            CHECK(count==2 && reply[0]==completed+1 && reply[1]==args[2],"gpu_store_reply");
            CHECK(words[i%256]==(u32)args[2],"gpu_store_buffer_copy");
            completed=reply[0];
        }
        MARK("PASS 300_gpu_stores_cpu_copyback shader_execution_tested=0");
    } else MARK("gpu_store_unavailable");
    if (info[1]&4) {
        MARK("direct_compute_batch_begin");
        for (unsigned i=0;i<64;i++) {
            /* Visit distinct 4KiB pages, including subpages of 16KiB VM pages.
             * Check the entire buffer: the shader may change only 256 words. */
            u64 offset=(i%(size/4096))*4096+128;
            for (u64 j=0;j<size/4;j++) words[j]=0xabad1deau^(u32)j;
            u64 args[4]={handle,offset,256,0x434f0000u+i},reply[2]={0};
            count=2;
            CHECK(call(connection,4,args,4,reply,&count)==0,"direct_compute_call");
            CHECK(count==2 && reply[0]==completed+1 && reply[1]==args[3],"direct_compute_reply");
            for (u64 j=0;j<size/4;j++) {
                u64 index=j-offset/4;
                u32 expected=index<256 ? (u32)args[3]+(u32)(index*index) : 0xabad1deau^(u32)j;
                CHECK(words[j]==expected,"direct_compute_output_or_guard");
            }
            completed=reply[0];
        }
        MARK("PASS 64_direct_compute 16384_shader_outputs full_buffer_guards");
    } else MARK("direct_compute_unavailable");
    CHECK(unmap(connection,(u32)handle,*task,address)==0,"second_unmap");
    mapped=0;
    count=0;
    CHECK(call(connection,2,&handle,1,0,&count)==0,"release");
    handleLive=0;
    CHECK(call(connection,2,&handle,1,0,&count)!=0,"double_release_accepted");
    MARK("map_readback_remap_release_pass");
    /* An existing mapping owns a descriptor reference independently of handle.
     * Do not touch it after IOServiceClose: actual VM teardown is tested separately. */
    count=2;
    CHECK(call(connection,1,&request,1,result,&count)==0,"retained_allocate");
    CHECK(result[0]!=handle && result[0]<=0xffffffffULL,"reused_handle");
    handle=result[0];handleLive=1;address=0;size=0;
    CHECK(map(connection,(u32)handle,*task,&address,&size,1)==0,"retained_map");
    mapped=1;
    CHECK(address && size>=request,"retained_map_size");
    words=(volatile u32 *)(usize)address;words[0]=0x13579bdf;
    count=0;
    CHECK(call(connection,2,&handle,1,0,&count)==0,"retained_release");
    handleLive=0;
    CHECK(words[0]==0x13579bdf,"retained_contents");
    words[0]=0x2468ace0;
    CHECK(words[0]==0x2468ace0,"retained_write");
    MARK("mapping_survives_handle_release_pass");
    success=1;
cleanup:
    if (peerMapped) unmap(peer,(u32)handle,*task,peerAddress);
    if (mapped && handleLive) unmap(connection,(u32)handle,*task,address);
    if (handleLive) { count=0; call(connection,2,&handle,1,0,&count); }
    if (peer && closeService(peer)!=0) { MARK("FAIL peer_close"); success=0; }
    if (connection && closeService(connection)!=0) { MARK("FAIL close"); success=0; }
    releaseObject(service);
    /* Process exit is the final cleanup boundary for any retained VM mapping. */
    if (success) MARK("PASS cpu_shared_storage (GPU_diagnostic_reported_separately)");
    return success?0:1;
}
