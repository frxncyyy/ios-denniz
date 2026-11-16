/* Exercise the production variant selection/cache with authored SPIR-V. */
#ifdef TEGU_TILE_FETCH_DEFAULT
static TeguAIRBlob tegu_tile_test_read(const char *directory,const char *name) {
 char path[4096];assert(snprintf(path,sizeof(path),"%s/%s",directory,name)<(int)sizeof(path));
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>20);rewind(f);
 TeguAIRBlob b={malloc((size_t)n),(size_t)n};assert(b.bytes&&fread(b.bytes,1,b.length,f)==b.length);fclose(f);return b;
}
static void test_tile_fetch_frontend(void) {
 const char *directory=getenv("TEGU_TILE_FETCH_FIXTURES");assert(directory);
 const char *previous=getenv("TEGU_TILE_FETCH");char *saved=previous?strdup(previous):0;
 setenv("TEGU_TILE_FETCH","1",1);
 TeguAIRBlob vs=tegu_tile_test_read(directory,"vertex.spv");
 const char *names[]={"memory.spv","memory.spv","cross_attachment.spv","no_fetch.spv","memory.spv"};
 for(unsigned test=0;test<5;test++) {
  TeguAIRBlob fs=tegu_tile_test_read(directory,names[test]);
  struct tegu_mali_graphics v={0},f={0};struct tegu_mali_graphics_pair2 pair={0};
  struct tegu_mali_generic_plan plan={0};char error[512]={0};
  assert(!tegu_mali_compile_generic_pair_rt(vs.bytes,vs.length,fs.bytes,fs.length,&v,&f,&pair,&plan,0,error,sizeof(error)));
  unsigned char *oldv=v.code,*oldf=f.code;
  if(test==4)setenv("TEGU_TILE_FETCH","0",1);
  tegu_lock(&tegu_compiler_lock);tegu_render_try_tile(0,&vs,&fs,&v,&f,&pair,&plan);tegu_unlock(&tegu_compiler_lock);
  if(test<2)assert(plan.reads_framebuffer==TEGU_MALI_GENERIC_FETCH_TILE&&v.code!=oldv&&f.code!=oldf);
  else assert(plan.reads_framebuffer!=TEGU_MALI_GENERIC_FETCH_TILE&&v.code==oldv&&f.code==oldf);
  tegu_mali_graphics_free(&v);tegu_mali_graphics_free(&f);free(fs.bytes);
 }
 free(vs.bytes);if(saved){setenv("TEGU_TILE_FETCH",saved,1);free(saved);}else unsetenv("TEGU_TILE_FETCH");
 puts("PASS tile frontend: distinct cached tile variant, cross-attachment/no-fetch fallback, runtime opt-out preserves memory artifacts");
}
#endif
