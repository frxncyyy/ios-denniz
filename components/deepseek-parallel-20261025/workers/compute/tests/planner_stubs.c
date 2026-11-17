/* Link-only stubs for a producer cdylib built before DELTA11's additive
 * dynamic-threadgroup planner symbols. check_frontend_delta5.py adds this
 * translation unit only when nm reports the real symbols absent, so a
 * rebuilt producer always exercises the real Rust planner. */
#include <stddef.h>
#include <stdint.h>
#include "air_bridge.h"
int32_t tegu_air_pipeline_native_dispatch_tg(const TeguAIRPipeline *pipeline,
 const TeguAIRBufferBinding *buffers,size_t count,const uint32_t lengths[31],
 const uint32_t groups[3],const uint32_t local[3],TeguAIRNativeDispatch *output,
 char *error,size_t cap) {
 (void)pipeline;(void)buffers;(void)count;(void)lengths;(void)groups;(void)local;
 (void)output;(void)error;(void)cap;return -1;
}
int32_t tegu_air_pipeline_native_dispatch_v2_tg(const TeguAIRPipeline *pipeline,
 const TeguAIRComputeResource *resources,size_t count,const uint32_t lengths[31],
 uint32_t mode,const uint32_t extents[3],const uint32_t local[3],
 TeguAIRBlob *variant_spirv,TeguAIRNativeDispatchV2 *output,char *error,size_t cap) {
 (void)pipeline;(void)resources;(void)count;(void)lengths;(void)mode;(void)extents;(void)local;
 (void)variant_spirv;(void)output;(void)error;(void)cap;return -1;
}
