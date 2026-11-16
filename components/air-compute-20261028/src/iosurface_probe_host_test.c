/* Exercise the native diagnostic's readback checks against bad observations.
 * These doubles test the observer, not IOSurface or GPU execution. */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long usize;
typedef void *object;
typedef void *selector;
#include "native_iosurface_render_probe.inc"
static unsigned memory[4096],expected[256];
static object texture,backing,reportedSurface;
static int lockFailure,unlockFailure,lockCount,unlockCount;
static usize allocationSize=16384;
static object get(object value,selector method) {
 assert(value==texture&&!strcmp(method,"iosurface"));return reportedSurface;
}
static selector sel(const char *name){return (void *)name;}
static void *base(object value){assert(value==backing);return memory;}
static usize allocation(object value){assert(value==backing);return allocationSize;}
static int lock(object value,unsigned flags,unsigned *seed) {
 assert(value==backing&&flags==0&&seed);lockCount++;return lockFailure;
}
static int unlock(object value,unsigned flags,unsigned *seed) {
 assert(value==backing&&flags==0&&seed);unlockCount++;return unlockFailure;
}
int main(void) {
 texture=(void *)1;backing=(void *)2;reportedSurface=backing;
 for(unsigned i=0;i<256;i++)expected[i]=memory[i]=0xff000000u|i;
 for(unsigned i=256;i<4096;i++)memory[i]=0xa55ac33cu;
 struct tegu_probe_surface state={.retained=backing,.borrowed=backing,
  .base=base,.allocation=allocation,.lock=lock,.unlock=unlock};
 assert(!tegu_probe_surface_readback(&state,texture,expected,get,sel));
 assert(lockCount==1&&unlockCount==1);
 /* A matching interior is insufficient: excluded raster edges must also
  * alias exactly between Metal readback and the direct CPU mapping. */
 memory[15]^=1;assert(tegu_probe_surface_readback(&state,texture,expected,get,sel));memory[15]^=1;
 memory[256]^=1;assert(tegu_probe_surface_readback(&state,texture,expected,get,sel));memory[256]^=1;
 memory[4095]^=1;assert(tegu_probe_surface_readback(&state,texture,expected,get,sel));memory[4095]^=1;
 lockFailure=1;int unlocked=unlockCount;
 assert(tegu_probe_surface_readback(&state,texture,expected,get,sel));assert(unlockCount==unlocked);lockFailure=0;
 unlockFailure=1;assert(tegu_probe_surface_readback(&state,texture,expected,get,sel));unlockFailure=0;
 allocationSize=1024;assert(tegu_probe_surface_readback(&state,texture,expected,get,sel));allocationSize=16384;
 reportedSurface=0;assert(tegu_probe_surface_readback(&state,texture,expected,get,sel));reportedSurface=backing;
 /* A caller release is permitted only because the texture still owns the
  * original surface. The driver lifetime tests verify that retain separately. */
 state.retained=0;assert(!tegu_probe_surface_readback(&state,texture,expected,get,sel));
 puts("Native IOSurface observer negative and shared-readback checks passed");
}
