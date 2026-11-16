#include "air_bridge.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

union OutputAlias {
 max_align_t alignment;
 unsigned char bytes[sizeof(TeguAIRRenderTypedPlan)];
};

int main(void) {
 TeguAIRRenderDraw draw={1,0,3,1,0};char error[64];
 union OutputAlias alias;memset(&alias,0xa5,sizeof(alias));unsigned char before[sizeof(alias)];memcpy(before,&alias,sizeof(alias));
 assert(tegu_air_render_pipeline_plan_draw_typed(0,0,0,&draw,
  (TeguAIRBlob *)alias.bytes,(TeguAIRRenderTypedPlan *)alias.bytes,error,sizeof(error))<0);
 assert(!memcmp(before,&alias,sizeof(alias)));

 TeguAIRBlob blob={(uint8_t *)(uintptr_t)0x1234,77};TeguAIRRenderTypedPlan plan;memset(&plan,0x6b,sizeof(plan));
 TeguAIRRenderTypedPlan planBefore;memcpy(&planBefore,&plan,sizeof(plan));
 assert(tegu_air_render_pipeline_plan_draw_typed(0,0,0,&draw,&blob,&plan,
  (char *)&plan,sizeof(plan))<0);
 assert(blob.bytes==(uint8_t *)(uintptr_t)0x1234&&blob.length==77&&!memcmp(&plan,&planBefore,sizeof(plan)));

 TeguAIRRenderResource resource;memset(&resource,0xc3,sizeof(resource));TeguAIRRenderResource resourceBefore=resource;
 memset(&blob,0x4d,sizeof(blob));TeguAIRBlob blobBefore=blob;memset(&plan,0x5e,sizeof(plan));memcpy(&planBefore,&plan,sizeof(plan));
 assert(tegu_air_render_pipeline_plan_draw_typed(0,&resource,1,&draw,&blob,&plan,
  (char *)&resource,sizeof(resource))<0);
 assert(!memcmp(&resource,&resourceBefore,sizeof(resource))&&!memcmp(&blob,&blobBefore,sizeof(blob))&&!memcmp(&plan,&planBefore,sizeof(plan)));

 /* A null/zero resource span is valid input; failure for the null pipeline
  * must clear its outputs without constructing a Rust slice from null. */
 memset(&blob,0xa1,sizeof(blob));memset(&plan,0xb2,sizeof(plan));
 assert(tegu_air_render_pipeline_plan_draw_typed(0,0,0,&draw,&blob,&plan,error,sizeof(error))<0);
 assert(!blob.bytes&&!blob.length&&!plan.version&&!plan.count);
 puts("PASS typed render C ABI alias and zero-count contracts");return 0;
}
