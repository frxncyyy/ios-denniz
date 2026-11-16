/* Optional shared-target qualification. Resolve public IOSurface C entry points
 * at runtime, keeping the probe independent of SDK headers and linker stubs.
 * API: https://developer.apple.com/documentation/iosurface/iosurface-functions
 * Locks below use the documented read/write options value zero. They provide
 * CPU access coordination; they do not establish a general Metal fence model. */
struct tegu_probe_surface {
 object retained, borrowed;
 void (*release)(object);
 void *(*base)(object);
 usize (*allocation)(object);
 int (*lock)(object,unsigned,unsigned *),(*unlock)(object,unsigned,unsigned *);
};
static object tegu_probe_surface_texture(struct tegu_probe_surface *s,object device,
 object descriptor,unsigned format,void *objc,void *send,selector (*sel)(const char *)) {
 void *framework=dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface",2);
 void *cf=dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",2);
 object (*create)(object)=framework?dlsym(framework,"IOSurfaceCreate"):0;
 usize (*width)(object)=framework?dlsym(framework,"IOSurfaceGetWidth"):0;
 usize (*height)(object)=framework?dlsym(framework,"IOSurfaceGetHeight"):0;
 usize (*stride)(object)=framework?dlsym(framework,"IOSurfaceGetBytesPerRow"):0;
 usize (*planes)(object)=framework?dlsym(framework,"IOSurfaceGetPlaneCount"):0;
 unsigned (*pixelFormat)(object)=framework?dlsym(framework,"IOSurfaceGetPixelFormat"):0;
 s->release=cf?dlsym(cf,"CFRelease"):0;
 s->base=framework?dlsym(framework,"IOSurfaceGetBaseAddress"):0;
 s->allocation=framework?dlsym(framework,"IOSurfaceGetAllocSize"):0;
 s->lock=framework?dlsym(framework,"IOSurfaceLock"):0;
 s->unlock=framework?dlsym(framework,"IOSurfaceUnlock"):0;
 object (*cls)(const char *)=dlsym(objc,"objc_getClass");
 if(!create||!width||!height||!stride||!planes||!pixelFormat||!s->release||
    !s->base||!s->allocation||!s->lock||!s->unlock||!cls)return 0;
 signed char (*responds)(object,selector,selector)=(void *)send;
 selector make=sel("newTextureWithDescriptor:iosurface:plane:");
 if(!responds(device,sel("respondsToSelector:"),make))return 0;
 object (*get)(object,selector)=(void *)send;
 object dictionary=get(cls("NSMutableDictionary"),sel("dictionary"));
 if(!dictionary)return 0;
 unsigned fourcc=format==80?0x42475241u:0x52474241u;
 const char *keys[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement",
  "IOSurfaceBytesPerRow","IOSurfaceAllocSize","IOSurfacePixelFormat"};
 const unsigned values[]={16,16,4,64,16384,fourcc};
 for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++) {
  object key=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),keys[i]);
  object value=((object (*)(object,selector,unsigned long long))send)(cls("NSNumber"),sel("numberWithUnsignedLongLong:"),values[i]);
  if(!key||!value)return 0;
  ((void (*)(object,selector,object,object))send)(dictionary,sel("setObject:forKey:"),value,key);
 }
 object key=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),"IOSurfaceTeguGPUShared");
 object value=((object (*)(object,selector,signed char))send)(cls("NSNumber"),sel("numberWithBool:"),1);
 if(!key||!value)return 0;
 ((void (*)(object,selector,object,object))send)(dictionary,sel("setObject:forKey:"),value,key);
 s->retained=create(dictionary);s->borrowed=s->retained;
 if(!s->retained)return 0;
 if(width(s->retained)!=16||height(s->retained)!=16||stride(s->retained)!=64||
    pixelFormat(s->retained)!=fourcc||planes(s->retained)!=0||
    s->allocation(s->retained)!=16384||!s->base(s->retained))return 0;
 unsigned seed=0;
 if(s->lock(s->retained,0,&seed))return 0;
 unsigned *pixels=s->base(s->retained);
 for(unsigned i=0;i<16384/4;i++)pixels[i]=i<256?0xffff7777u:0xa55ac33cu;
 if(s->unlock(s->retained,0,&seed))return 0;
 object texture=((object (*)(object,selector,object,object,usize))send)(device,make,descriptor,s->retained,0);
 if(texture&&(get(texture,sel("iosurface"))!=s->retained||
    ((usize (*)(object,selector))send)(texture,sel("iosurfacePlane"))!=0)) {
  ((void (*)(object,selector))send)(texture,sel("release"));return 0;
 }
 return texture;
}
static int tegu_probe_surface_readback(struct tegu_probe_surface *s,object texture,
 const unsigned *metalPixels,void *send,selector (*sel)(const char *)) {
 object surface=((object (*)(object,selector))send)(texture,sel("iosurface"));
 if(!surface||surface!=s->borrowed||s->allocation(surface)!=16384)return 1;
 unsigned seed=0;if(s->lock(surface,0,&seed))return 1;
 const unsigned *mapped=s->base(surface);unsigned mismatches=0,guards=0;
 if(!mapped){s->unlock(surface,0,&seed);return 1;}
 for(unsigned i=0;i<256;i++)mismatches+=mapped[i]!=metalPixels[i];
 for(unsigned i=256;i<16384/4;i++)guards+=mapped[i]!=0xa55ac33cu;
 int failed=s->unlock(surface,0,&seed)!=0||mismatches||guards;
 printf("TEGU_IOSURFACE_READBACK pixels=256 alias_mismatches=%u tail_words=3840 tail_mismatches=%u caller_reference=%d\n",
  mismatches,guards,s->retained!=0);
 return failed;
}
