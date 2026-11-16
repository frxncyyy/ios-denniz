/* Minimal host syntax stub for the native diagnostic client.  Not used on iOS. */
#ifndef NORD2_TEST_IOKITLIB_H
#define NORD2_TEST_IOKITLIB_H
#include <stdint.h>
typedef unsigned io_service_t;
typedef unsigned io_connect_t;
typedef unsigned kern_return_t;
typedef unsigned mach_port_t;
#define kIOMasterPortDefault 0u
static inline mach_port_t mach_task_self(void) { return 0; }
static inline io_service_t IOServiceGetMatchingService(mach_port_t port,
                                                        const void *match) {
        (void)port; (void)match; return 0;
}
static inline void *IOServiceMatching(const char *name) {
        (void)name; return 0;
}
static inline kern_return_t IOServiceOpen(io_service_t service,
                                          mach_port_t task, unsigned type,
                                          io_connect_t *conn) {
        (void)service; (void)task; (void)type; (void)conn; return 1;
}
static inline kern_return_t IOServiceClose(io_connect_t conn) {
        (void)conn; return 0;
}
static inline kern_return_t IOObjectRelease(io_service_t obj) {
        (void)obj; return 0;
}
static inline kern_return_t IOConnectCallMethod(
        io_connect_t conn, uint32_t selector, const uint64_t *input,
        uint32_t inputCnt, const void *inputStruct, size_t inputStructCnt,
        uint64_t *output, uint32_t *outputCnt, void *outputStruct,
        size_t *outputStructCnt) {
        (void)conn; (void)selector; (void)input; (void)inputCnt;
        (void)inputStruct; (void)inputStructCnt; (void)output;
        (void)outputCnt; (void)outputStruct; (void)outputStructCnt;
        return 1;
}
#endif
