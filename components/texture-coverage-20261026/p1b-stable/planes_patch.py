#!/usr/bin/env python3
"""texture-opus P1b: sampled-only IOSurface planes (YUV '420v'/'420f'/'x420',
b3a8 planes) and bytes-per-element from IOSurface itself."""
from pathlib import Path
import sys
T = Path(sys.argv[1])
p = T / 'render_encoder.inc'; s = p.read_text()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (s.count(old), old[:80])
    s = s.replace(old, new)
rep(""" uint32_t (*getSeed)(void *); /* optional: shadow refresh skip */
};""", """ uint32_t (*getSeed)(void *); /* optional: shadow refresh skip */
 /* Optional (sampled-only shadows): bytes per element and per-plane facts. */
 size_t (*getBytesPerElement)(void *);
 void *(*getPlaneBase)(void *,size_t);
 size_t (*getPlaneRow)(void *,size_t),(*getPlaneWidth)(void *,size_t),(*getPlaneHeight)(void *,size_t),(*getPlaneElement)(void *,size_t);
};""")
rep("""   loaded.getSeed=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetSeed");}""",
"""   loaded.getSeed=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetSeed");
   loaded.getBytesPerElement=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetBytesPerElement");
   loaded.getPlaneBase=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetBaseAddressOfPlane");
   loaded.getPlaneRow=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetBytesPerRowOfPlane");
   loaded.getPlaneWidth=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetWidthOfPlane");
   loaded.getPlaneHeight=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetHeightOfPlane");
   loaded.getPlaneElement=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetBytesPerElementOfPlane");}""")
rep(""" NSUInteger apiFormat;unsigned shadowConvert;
}""", """ NSUInteger apiFormat;unsigned shadowConvert;
 /* Sampled-only shadow of one plane of a multi-plane IOSurface: plane+1
  * (0 = the whole single-plane surface). */
 NSUInteger shadowPlane;
}""")
rep(""" if(src->shadowConvert){v->format=src->format;v->apiFormat=src->apiFormat;v->shadowConvert=src->shadowConvert;}""",
""" if(src->shadowConvert){v->format=src->format;v->apiFormat=src->apiFormat;v->shadowConvert=src->shadowConvert;}
 v->shadowPlane=src->shadowPlane;""")
rep("""- (NSUInteger)iosurfacePlane{return 0;}""", """- (NSUInteger)iosurfacePlane{return shadowPlane?shadowPlane-1:0;}""")
# Shared plane geometry helper before tegu_shadow_convert_cpu.
rep("""static unsigned tegu_xr_source_bpp_of(unsigned kind);
static int tegu_shadow_convert_cpu(""", """static unsigned tegu_xr_source_bpp_of(unsigned kind);
/* Base/row/width/height of the bytes a shadow mirrors: the whole surface, or
 * plane shadowPlane-1. Callers hold the surface lock for `base`. */
static int tegu_shadow_geometry(const struct TeguIOSurfaceAPI *api,void *surface,NSUInteger shadowPlane,
 unsigned char **base,size_t *row,size_t *width,size_t *height,size_t *alloc) {
 if(!api||!surface)return 0;
 if(!shadowPlane) {
  *row=api->getBytesPerRow(surface);*width=api->getWidth(surface);*height=api->getHeight(surface);
  *alloc=api->getAllocSize(surface);if(base)*base=(unsigned char *)api->getBaseAddress(surface);return 1;
 }
 size_t plane=shadowPlane-1;
 if(!api->getPlaneBase||!api->getPlaneRow||!api->getPlaneWidth||!api->getPlaneHeight||plane>=api->getPlaneCount(surface))return 0;
 *row=api->getPlaneRow(surface,plane);*width=api->getPlaneWidth(surface,plane);*height=api->getPlaneHeight(surface,plane);
 *alloc=*row*(*height);if(base)*base=(unsigned char *)api->getPlaneBase(surface,plane);return 1;
}
static int tegu_shadow_convert_cpu(""")
rep(""" NSUInteger row=api->getBytesPerRow(surface);
 if(api->getWidth(surface)<t->textureWidth||api->getHeight(surface)<t->textureHeight||row<t->textureWidth*bpp||
    api->getAllocSize(surface)/row<t->textureHeight)return 0;
 if(api->lock(surface,write?0:1,&seed))return 0;
 unsigned char *base=(unsigned char *)api->getBaseAddress(surface);""",
""" size_t row=0,sw=0,sh=0,salloc=0;unsigned char *base=0;
 if(!tegu_shadow_geometry(api,surface,t->shadowPlane,0,&row,&sw,&sh,&salloc)||!row||
    sw<t->textureWidth||sh<t->textureHeight||row<t->textureWidth*bpp||salloc/row<t->textureHeight)return 0;
 if(api->lock(surface,write?0:1,&seed))return 0;
 if(!tegu_shadow_geometry(api,surface,t->shadowPlane,&base,&row,&sw,&sh,&salloc))base=0;""")
# Sampled-only import: planes and element size.
rep(""" if((self->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&d&&surface&&plane==0&&[d usage]==1&&[d textureType]==2&&""",
""" if((self->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&d&&surface&&plane<4&&[d usage]==1&&[d textureType]==2&&""")
rep("""  unsigned convert=tegu_xr_shadow_kind(format);
  NSUInteger bpp=convert?tegu_xr_source_bpp(convert):tegu_texture_bytes_per_pixel(format),width=[d width],height=[d height];
  int layout=bpp&&tegu_iosurface_fourcc_bpp(fourcc)==bpp;
  NSUInteger row=api?api->getBytesPerRow((void *)surface):0,alloc=api?api->getAllocSize((void *)surface):0;
  if(layout&&api&&api->getPlaneCount((void *)surface)==0&&api->getWidth((void *)surface)==width&&
     api->getHeight((void *)surface)==height&&bpp&&row>=width*bpp&&row<=65536&&height<=SIZE_MAX/row&&alloc>=row*height) {""",
"""  unsigned convert=tegu_xr_shadow_kind(format);
  NSUInteger bpp=convert?tegu_xr_source_bpp(convert):tegu_texture_bytes_per_pixel(format),width=[d width],height=[d height];
  /* One plane of a multi-plane surface ('420v'/'420f'/'x420' luma/chroma,
   * 'b3a8' colour/alpha) or the whole single-plane surface. Apple's
   * lossless-compressed surfaces ('&...' fourccs) have no linear bytes. */
  size_t planes=api?api->getPlaneCount((void *)surface):0,row=0,sw=0,sh=0,alloc=0;
  NSUInteger shadowPlane=planes?plane+1:0;
  int geometry=api&&(planes?plane<planes:plane==0)&&(fourcc>>24)!=0x26u&&
   tegu_shadow_geometry(api,(void *)surface,shadowPlane,0,&row,&sw,&sh,&alloc);
  size_t element=!geometry?0:planes?(api->getPlaneElement?api->getPlaneElement((void *)surface,plane):0):
   (api->getBytesPerElement?api->getBytesPerElement((void *)surface):0);
  if(geometry&&!element&&!planes)element=tegu_iosurface_fourcc_bpp(fourcc);
  int layout=bpp&&element==bpp;
  if(layout&&sw==width&&sh==height&&row>=width*bpp&&row<=65536&&height<=SIZE_MAX/row&&alloc>=row*height) {""")
rep("""    if(convert){shadow->apiFormat=format;shadow->shadowConvert=convert;}""",
"""    if(convert){shadow->apiFormat=format;shadow->shadowConvert=convert;}
    shadow->shadowPlane=shadowPlane;""")
rep("""  if(!layout){tegu_iosurface_refused(d,surface,plane,bpp?"sampled-fourcc-size-mismatch":"sampled-format-unsupported");return 0;}""",
"""  if(!layout||geometry){tegu_iosurface_refused(d,surface,plane,!geometry?"sampled-plane-unavailable":
   !bpp?"sampled-format-unsupported":!layout?"sampled-fourcc-size-mismatch":"sampled-plane-shape");return 0;}""")
p.write_text(s)
c = T / 'compositor_submit.inc'; s = c.read_text()
rep(""" void *surface=(void *)t->shadowSurface;
 size_t row=api->getBytesPerRow(surface),width=t->textureWidth*bpp,height=t->textureHeight;
 size_t sourceWidth=t->textureWidth*sourceBpp;
 if(api->getWidth(surface)<t->textureWidth||api->getHeight(surface)<height||row<sourceWidth||
    api->getAllocSize(surface)/row<height||t->rowBytes<width||
    t->mip[0].offset+(height-1)*t->rowBytes+width>t->buffer->logical)return 0;
 t->shadowSeedValid=0;
 if(api->lock(surface,toSurface?0:1,&seed))return 0;
 unsigned char *s=(unsigned char *)api->getBaseAddress(surface),*b=(unsigned char *)tegu_buffer_bytes(t->buffer);""",
""" void *surface=(void *)t->shadowSurface;
 size_t row=0,sw=0,sh=0,salloc=0,width=t->textureWidth*bpp,height=t->textureHeight;
 size_t sourceWidth=t->textureWidth*sourceBpp;
 /* A plane shadow (shadowPlane) mirrors one plane and is sampled-only. */
 if((t->shadowPlane&&toSurface)||!tegu_shadow_geometry(api,surface,t->shadowPlane,0,&row,&sw,&sh,&salloc)||!row||
    sw<t->textureWidth||sh<height||row<sourceWidth||salloc/row<height||t->rowBytes<width||
    t->mip[0].offset+(height-1)*t->rowBytes+width>t->buffer->logical)return 0;
 t->shadowSeedValid=0;
 if(api->lock(surface,toSurface?0:1,&seed))return 0;
 unsigned char *s=0,*b=(unsigned char *)tegu_buffer_bytes(t->buffer);
 if(!tegu_shadow_geometry(api,surface,t->shadowPlane,&s,&row,&sw,&sh,&salloc))s=0;""")
c.write_text(s)
print('planes patch applied')
