#version 450
/* Two-plane YCbCr IOSurface -> owned decode shadow, one full-target draw per
 * new or changed surface (compositor_submit.inc tegu_shadow_gpu_*; A3).
 * The exact integer model of metal/tegu/ycbcr_decode.h, which is bit-exact
 * against Metal: no filtering hardware, no float colour maths.
 *   binding 0: luma plane codes as R8Uint / R16Uint (10 bits in the high bits)
 *              / R32Uint (packed, three 10-bit codes per word)
 *   binding 1: chroma plane as RG8Uint / RG16Uint (Cb, Cr) / R32Uint (packed)
 *   binding 2: the 513 sRGB knots (tegu_srgb_knots_8192) as R16Uint
 * The target pixel is the texel: gl_FragCoord addresses it (as the pre-frame
 * preload does), the preload vertex shader covers the target. */
layout(set=0,binding=0) uniform usampler2D lumaPlane;
layout(set=0,binding=1) uniform usampler2D chromaPlane;
layout(set=0,binding=2) uniform usampler2D srgbKnots;
layout(push_constant) uniform Params {
 ivec4 dims; /* chroma width, chroma height, -, - */
 ivec4 form; /* bits (8/10), packed, sub (0 420, 1 422, 2 444), output (0 converted, 1 sRGB, 2 codes) */
 ivec4 ka;   /* k[0][0] k[0][1] k[1][0] k[1][1] */
 ivec4 kb;   /* k[2][0] k[2][1] full_range - */
} p;
layout(location=0) out vec4 color;

uint packedCode(usampler2D plane,int index,int row){
 uint w=texelFetch(plane,ivec2(index/3,row),0).r;
 return (w>>(10u*uint(index%3)))&1023u;
}
uint lumaCode(int x,int y){
 if(p.form.y!=0)return packedCode(lumaPlane,x,y);
 uint v=texelFetch(lumaPlane,ivec2(x,y),0).r;
 return p.form.x==8?v:v>>6;
}
uvec2 chromaPair(int cx,int cy){
 if(p.form.y!=0)return uvec2(packedCode(chromaPlane,2*cx,cy),packedCode(chromaPlane,2*cx+1,cy));
 uvec2 v=texelFetch(chromaPlane,ivec2(cx,cy),0).rg;
 return p.form.x==8?v:v>>6;
}
/* sRGB knots interpolation, nearest-even on the 1/8192 grid, then the value
 * as IEEE half with ties away from zero (tegu_ycbcr_half(step,13)). The
 * result is exactly representable as half, so the RGBA16F store is exact. */
float srgbValue(int T){
 int q=clamp((T+512)>>10,0,8192);
 int i=min(q>>4,511),f=q-16*i;
 uint k0=texelFetch(srgbKnots,ivec2(i,0),0).r,k1=texelFetch(srgbKnots,ivec2(i+1,0),0).r;
 uint num=16u*k0+(k1-k0)*uint(f);
 uint step=num>>4,rem=num&15u;
 if(rem>8u||(rem==8u&&(step&1u)!=0u))step++;
 if(step==0u)return 0.0;
 int msb=findMSB(step);
 if(msb>10){int drop=msb-10;step=((step+(1u<<uint(drop-1)))>>uint(drop))<<uint(drop);}
 return float(step)/8192.0;
}
void main(){
 int x=int(gl_FragCoord.x),y=int(gl_FragCoord.y);
 int cw=p.dims.x,ch=p.dims.y;
 /* Chroma 4:2:0 9/3/3/1 of the four nearest pairs, 4:2:2 3/1 across,
  * 4:4:4 its own pair; edge pairs clamp. C = (S + 8) >> 4. */
 int cx0,cx1,cy0,cy1;uint wx0,wx1,wy0,wy1;
 if(p.form.z!=2){
  int near=x/2,far=(x&1)!=0?near+1:near-1;
  cx1=clamp(far,0,cw-1);cx0=min(near,cw-1);wx0=3u;wx1=1u;
 } else {cx0=cx1=min(x,cw-1);wx0=4u;wx1=0u;}
 if(p.form.z==0){
  int near=y/2,far=(y&1)!=0?near+1:near-1;
  cy1=clamp(far,0,ch-1);cy0=min(near,ch-1);wy0=3u;wy1=1u;
 } else {cy0=cy1=min(y,ch-1);wy0=4u;wy1=0u;}
 uvec2 s=wx0*wy0*chromaPair(cx0,cy0)+wx1*wy0*chromaPair(cx1,cy0)+
  wx0*wy1*chromaPair(cx0,cy1)+wx1*wy1*chromaPair(cx1,cy1);
 uvec2 C=(s+8u)>>4;
 uint Y=lumaCode(x,y);
 if(p.form.w==2){
  /* Codes: red Cr, green Y, blue Cb, alpha 1 (RGBA8 or RGB10A2 texels). */
  float m=p.form.x==8?255.0:1023.0;
  color=vec4(float(C.y)/m,float(Y)/m,float(C.x)/m,1.0);
  return;
 }
 uint u=uint(10-p.form.x);
 int L=p.kb.z!=0?256*int(p.form.x==8?(Y*32768u+127u)/255u:(Y*32768u+511u)/1023u):9576*(int(Y<<u)-64);
 int cb=int(C.x<<u)-512,cr=int(C.y<<u)-512;
 int T0=L+p.ka.x*cb+p.ka.y*cr,T1=L+p.ka.z*cb+p.ka.w*cr,T2=L+p.kb.x*cb+p.kb.y*cr;
 if(p.form.w==1){color=vec4(srgbValue(T0),srgbValue(T1),srgbValue(T2),1.0);return;}
 /* Converted: v = clamp((T + 2048) >> 12, 0, 2048) / 2048, stored as the
  * RGB10A2 shadow's code round(v * 1023 / 2048) (tegu_ycbcr_code10). */
 ivec3 v=clamp((ivec3(T0,T1,T2)+2048)>>12,0,2048);
 uvec3 code=(uvec3(v)*1023u+1024u)>>11;
 color=vec4(vec3(code)/1023.0,1.0);
}
