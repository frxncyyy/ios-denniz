/* On-device compiler test. Optional Mali backend executes the qualified fixture
 * only after exact comparison to its previously validated pipeline package.
 * This does not implement generic compiler metadata/resource binding yet. */
#include "air_bridge.h"
#include "tegu-air-fixture.h"
#ifdef TEGU_MALI_BRIDGE
#include "mali_compute_compiler.h"
#include "tegu-pipeline-fixture.h"
extern int tegu_frontend_package_probe(const void *, size_t);
#endif
extern long write(int,const void *,size_t);
#define MARK(s) ((void)write(2,"TEGU_AIR " s "\n",sizeof("TEGU_AIR " s "\n")-1))
static int same(const unsigned char *a,const unsigned char *b,size_t n) {
 for(size_t i=0;i<n;i++) if(a[i]!=b[i])return 0;
 return 1;
}
int tegu_air_native_probe(void) {
 MARK("begin");
 char error[512]={0};
 TeguAIRLibrary *library=tegu_air_library_open(tegu_air_fixture,sizeof(tegu_air_fixture),error,sizeof(error));
 if(!library) { MARK("FAIL open"); return 1; }
 MARK("library_opened");
 int ok=tegu_air_function_count(library)==5;
 const unsigned char name[]="scale_and_offset";
 TeguAIRBlob spirv={0},metadata={0};
 MARK("compile_begin");
 int rc=tegu_air_compile_compute(library,name,sizeof(name)-1,tegu_air_profile,sizeof(tegu_air_profile),&spirv,&metadata,error,sizeof(error));
 MARK("compile_returned");
 if(rc) {
  MARK("FAIL compile");
  size_t n=0;while(n<sizeof(error)&&error[n])n++;
  write(2,error,n);write(2,"\n",1);ok=0;
 } else if(spirv.length!=sizeof(tegu_air_expected_spirv)||!same(spirv.bytes,tegu_air_expected_spirv,spirv.length)||!metadata.length) {
  MARK("FAIL output_comparison");ok=0;
 }
#ifdef TEGU_MALI_BRIDGE
 if(ok) {
  struct tegu_mali_blob package={0};
  MARK("Mali_compile_begin");
  rc=tegu_mali_compile(spirv.bytes,spirv.length,&package,error,sizeof(error));
  if(rc) {
   MARK("FAIL Mali_compile");
   size_t n=0;while(n<sizeof(error)&&error[n])n++;
   write(2,error,n);write(2,"\n",1);ok=0;
  } else if(package.size!=sizeof(tegu_pipeline_fixture)||!same(package.data,tegu_pipeline_fixture,package.size)) {
   MARK("FAIL Mali_package_comparison");ok=0;
  } else {
   MARK("runtime_package_GPU_begin");
   ok=tegu_frontend_package_probe(package.data,package.size)==0;
   if(ok) MARK("PASS runtime_AIR_to_SPIRV_to_Mali GPU_outputs=1024 qualified_fixture_only");
  }
  tegu_mali_free(&package);
 }
#endif
 tegu_air_blob_free(&spirv);tegu_air_blob_free(&metadata);tegu_air_library_close(library);
#ifndef TEGU_MALI_BRIDGE
 if(ok) MARK("PASS runtime_AIR_to_SPIRV exact_host_comparison GPU_execution_tested=0");
#endif
 return ok?0:1;
}
