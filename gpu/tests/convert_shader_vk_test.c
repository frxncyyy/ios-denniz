/* Host GPU check of the A3 surface-conversion shaders (no Mali claim).
 *
 * Runs the SAME SPIR-V that tools/generate_convert.c feeds the pinned arch9
 * compiler (tests/shaders/nord2_preload.vert + nord2_{ycbcr,xr}_convert.frag)
 * on the host's Vulkan device, with the frontend's exact staging layout
 * (uint plane views, the knot/half tables, the push block of
 * compositor_submit.inc tegu_shadow_gpu_convert), into the shadow's own
 * format (RGB10A2 / RGBA16F / RGBA8), and compares every texel with the
 * production CPU decoders (ycbcr_decode.h, tegu_xr_decode_row's table walk).
 * This proves the shader's integer model, not the Mali binary: the Mali
 * compile is covered by generate_convert's plan asserts and the frontend
 * tests' fake-kernel submission.
 *
 * Usage: convert_shader_vk_test preload.vert.spv ycbcr_convert.frag.spv xr_convert.frag.spv
 * Prints CONVERT_VK ycbcr=<cases> xr=<cases> texels=<n> mismatches=0 on success;
 * exit 77 (skip) when no Vulkan device is present. */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tegu/ycbcr_decode.h"
#include "tegu/xr_decode_table.h"
/* render_encoder.inc tegu_ycbcr_row_bytes: bytes a row of `count` codes occupies. */
static size_t tegu_ycbcr_row_bytes(const struct tegu_ycbcr_layout *l,size_t count){
 return l->packed?((count+2)/3)*4:count*(l->bits==8?1u:2u);
}

#define CK(x) do{VkResult r_=(x);if(r_){fprintf(stderr,"%s:%d %s -> %d\n",__FILE__,__LINE__,#x,r_);exit(1);}}while(0)
static VkDevice dev;static VkPhysicalDevice phys;static VkQueue queue;static uint32_t qfam;
static VkCommandPool pool;
static void *read_file(const char *path,size_t *size){
 FILE *f=fopen(path,"rb");if(!f){perror(path);exit(1);}fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 void *p=malloc(n);if(fread(p,1,n,f)!=(size_t)n)exit(1);fclose(f);*size=n;return p;
}
static uint32_t mem_type(uint32_t bits,VkMemoryPropertyFlags want){
 VkPhysicalDeviceMemoryProperties m;vkGetPhysicalDeviceMemoryProperties(phys,&m);
 for(uint32_t i=0;i<m.memoryTypeCount;i++)if((bits&(1u<<i))&&(m.memoryTypes[i].propertyFlags&want)==want)return i;
 fprintf(stderr,"no memory type\n");exit(1);
}
struct buf{VkBuffer b;VkDeviceMemory m;void *p;};
static struct buf buffer(VkDeviceSize size,VkBufferUsageFlags use){
 struct buf r;VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,0,0,size,use,VK_SHARING_MODE_EXCLUSIVE,0,0};
 CK(vkCreateBuffer(dev,&ci,0,&r.b));VkMemoryRequirements q;vkGetBufferMemoryRequirements(dev,r.b,&q);
 VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,q.size,mem_type(q.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
 CK(vkAllocateMemory(dev,&ai,0,&r.m));CK(vkBindBufferMemory(dev,r.b,r.m,0));CK(vkMapMemory(dev,r.m,0,VK_WHOLE_SIZE,0,&r.p));return r;
}
static void buffer_free(struct buf *b){vkUnmapMemory(dev,b->m);vkDestroyBuffer(dev,b->b,0);vkFreeMemory(dev,b->m,0);}
struct img{VkImage i;VkDeviceMemory m;VkImageView v;uint32_t w,h,bpt;VkFormat f;};
static struct img image(VkFormat f,uint32_t w,uint32_t h,uint32_t bpt,VkImageUsageFlags use){
 struct img r={0};r.w=w;r.h=h;r.bpt=bpt;r.f=f;
 VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,0,0,VK_IMAGE_TYPE_2D,f,{w,h,1},1,1,VK_SAMPLE_COUNT_1_BIT,
  VK_IMAGE_TILING_OPTIMAL,use|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_SHARING_MODE_EXCLUSIVE,0,0,VK_IMAGE_LAYOUT_UNDEFINED};
 CK(vkCreateImage(dev,&ci,0,&r.i));VkMemoryRequirements q;vkGetImageMemoryRequirements(dev,r.i,&q);
 VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,q.size,mem_type(q.memoryTypeBits,0)};
 CK(vkAllocateMemory(dev,&ai,0,&r.m));CK(vkBindImageMemory(dev,r.i,r.m,0));
 VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,0,0,r.i,VK_IMAGE_VIEW_TYPE_2D,f,{0},{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 CK(vkCreateImageView(dev,&vi,0,&r.v));return r;
}
static void image_free(struct img *i){vkDestroyImageView(dev,i->v,0);vkDestroyImage(dev,i->i,0);vkFreeMemory(dev,i->m,0);}
static void barrier(VkCommandBuffer c,VkImage i,VkImageLayout from,VkImageLayout to){
 VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,0,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
  from,to,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,i,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,0,0,0,1,&b);
}
/* Upload tightly packed rows (bpt bytes per texel) into a sampled image. */
static void upload(VkCommandBuffer c,struct img *i,struct buf *s,VkDeviceSize off){
 barrier(c,i->i,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 VkBufferImageCopy r={off,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{i->w,i->h,1}};
 vkCmdCopyBufferToImage(c,s->b,i->i,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
 barrier(c,i->i,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
static VkShaderModule module(const char *path){
 size_t n;void *code=read_file(path,&n);VkShaderModuleCreateInfo ci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,0,0,n,code};
 VkShaderModule m;CK(vkCreateShaderModule(dev,&ci,0,&m));free(code);return m;
}
static VkShaderModule vs_mod,ycc_mod,xr_mod;static VkSampler sampler;
/* Draw the full-target triangle with `textures` combined samplers and the
 * push block into `target`, then read it back tightly packed into `out`. */
static void run(VkShaderModule fs,struct img *tex,unsigned textures,const void *push,uint32_t push_bytes,struct img *target,void *out){
 VkDescriptorSetLayoutBinding lb[3];
 for(unsigned i=0;i<textures;i++)lb[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,0};
 VkDescriptorSetLayoutCreateInfo lci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,0,0,textures,lb};
 VkDescriptorSetLayout sl;CK(vkCreateDescriptorSetLayout(dev,&lci,0,&sl));
 VkPushConstantRange pr={VK_SHADER_STAGE_FRAGMENT_BIT,0,push_bytes};
 VkPipelineLayoutCreateInfo plci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,0,0,1,&sl,1,&pr};
 VkPipelineLayout pl;CK(vkCreatePipelineLayout(dev,&plci,0,&pl));
 VkAttachmentDescription at={0,target->f,VK_SAMPLE_COUNT_1_BIT,VK_ATTACHMENT_LOAD_OP_DONT_CARE,VK_ATTACHMENT_STORE_OP_STORE,
  VK_ATTACHMENT_LOAD_OP_DONT_CARE,VK_ATTACHMENT_STORE_OP_DONT_CARE,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
 VkAttachmentReference ar={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkSubpassDescription sp={0,VK_PIPELINE_BIND_POINT_GRAPHICS,0,0,1,&ar,0,0,0,0};
 VkRenderPassCreateInfo rpci={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,0,0,1,&at,1,&sp,0,0};
 VkRenderPass rp;CK(vkCreateRenderPass(dev,&rpci,0,&rp));
 VkFramebufferCreateInfo fci={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,0,0,rp,1,&target->v,target->w,target->h,1};
 VkFramebuffer fb;CK(vkCreateFramebuffer(dev,&fci,0,&fb));
 VkPipelineShaderStageCreateInfo st[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,0,0,VK_SHADER_STAGE_VERTEX_BIT,vs_mod,"main",0},
  {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,0,0,VK_SHADER_STAGE_FRAGMENT_BIT,fs,"main",0}};
 VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
 VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,0,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,0};
 VkViewport vp={0,0,(float)target->w,(float)target->h,0,1};VkRect2D sc={{0,0},{target->w,target->h}};
 VkPipelineViewportStateCreateInfo vs={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,0,0,1,&vp,1,&sc};
 VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,0,0,0,0,VK_POLYGON_MODE_FILL,VK_CULL_MODE_NONE,
  VK_FRONT_FACE_COUNTER_CLOCKWISE,0,0,0,0,1};
 VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,0,0,VK_SAMPLE_COUNT_1_BIT};
 VkPipelineColorBlendAttachmentState cb={0};cb.colorWriteMask=15;
 VkPipelineColorBlendStateCreateInfo cbs={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,0,0,0,0,1,&cb,{0}};
 VkGraphicsPipelineCreateInfo gci={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,0,0,2,st,&vi,&ia,0,&vs,&rs,&ms,0,&cbs,0,pl,rp,0,0,0};
 VkPipeline pipe;CK(vkCreateGraphicsPipelines(dev,0,1,&gci,0,&pipe));
 VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,textures};
 VkDescriptorPoolCreateInfo dpci={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,0,0,1,1,&ps};
 VkDescriptorPool dp;CK(vkCreateDescriptorPool(dev,&dpci,0,&dp));
 VkDescriptorSetAllocateInfo dai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,0,dp,1,&sl};
 VkDescriptorSet ds;CK(vkAllocateDescriptorSets(dev,&dai,&ds));
 VkDescriptorImageInfo ii[3];VkWriteDescriptorSet wr[3];
 for(unsigned i=0;i<textures;i++){ii[i]=(VkDescriptorImageInfo){sampler,tex[i].v,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  wr[i]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,0,ds,i,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&ii[i],0,0};}
 vkUpdateDescriptorSets(dev,textures,wr,0,0);
 VkDeviceSize rb=(VkDeviceSize)target->w*target->h*target->bpt;struct buf rd=buffer(rb,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,0,pool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
 VkCommandBuffer c;CK(vkAllocateCommandBuffers(dev,&cai,&c));
 VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,0,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,0};
 CK(vkBeginCommandBuffer(c,&bi));
 VkRenderPassBeginInfo rbi={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,0,rp,fb,{{0,0},{target->w,target->h}},0,0};
 vkCmdBeginRenderPass(c,&rbi,VK_SUBPASS_CONTENTS_INLINE);
 vkCmdBindPipeline(c,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
 vkCmdBindDescriptorSets(c,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,1,&ds,0,0);
 vkCmdPushConstants(c,pl,VK_SHADER_STAGE_FRAGMENT_BIT,0,push_bytes,push);
 vkCmdDraw(c,3,1,0,0);vkCmdEndRenderPass(c);
 VkBufferImageCopy r={0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{target->w,target->h,1}};
 vkCmdCopyImageToBuffer(c,target->i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rd.b,1,&r);
 CK(vkEndCommandBuffer(c));
 VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,0,0,0,0,1,&c,0,0};CK(vkQueueSubmit(queue,1,&si,0));CK(vkQueueWaitIdle(queue));
 memcpy(out,rd.p,rb);buffer_free(&rd);
 vkFreeCommandBuffers(dev,pool,1,&c);vkDestroyDescriptorPool(dev,dp,0);vkDestroyPipeline(dev,pipe,0);vkDestroyFramebuffer(dev,fb,0);
 vkDestroyRenderPass(dev,rp,0);vkDestroyPipelineLayout(dev,pl,0);vkDestroyDescriptorSetLayout(dev,sl,0);
}
static void upload_all(struct img *tex,unsigned n,const void *const *data){
 VkDeviceSize total=0,off[3];for(unsigned i=0;i<n;i++){off[i]=total;total+=(VkDeviceSize)tex[i].w*tex[i].h*tex[i].bpt;total=(total+15)&~15ull;}
 struct buf s=buffer(total,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
 for(unsigned i=0;i<n;i++)memcpy((char *)s.p+off[i],data[i],(size_t)tex[i].w*tex[i].h*tex[i].bpt);
 VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,0,pool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
 VkCommandBuffer c;CK(vkAllocateCommandBuffers(dev,&cai,&c));
 VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,0,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,0};
 CK(vkBeginCommandBuffer(c,&bi));for(unsigned i=0;i<n;i++)upload(c,&tex[i],&s,off[i]);CK(vkEndCommandBuffer(c));
 VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,0,0,0,0,1,&c,0,0};CK(vkQueueSubmit(queue,1,&si,0));CK(vkQueueWaitIdle(queue));
 vkFreeCommandBuffers(dev,pool,1,&c);buffer_free(&s);
}
static uint32_t rng=0x12345678u;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned long texels,mismatches;
/* One YCbCr case: Metal format, range, matrix and a w x h surface. The
 * planes hold random codes (rows exactly as the frontend copies them). */
static void ycbcr_case(uint32_t format,unsigned full,unsigned matrix,unsigned w,unsigned h,unsigned special){
 struct tegu_ycbcr_layout l;assert(tegu_ycbcr_format(format,&l));l.full_range=full;l.matrix=matrix;
 unsigned cw=l.sub==TEGU_YCBCR_SUB_444?w:(w+1)/2,ch=l.sub==TEGU_YCBCR_SUB_420?(h+1)/2:h;
 size_t lrow=tegu_ycbcr_row_bytes(&l,w),crow=tegu_ycbcr_row_bytes(&l,2*(size_t)cw);
 uint8_t *luma=calloc(lrow,h),*chroma=calloc(crow,ch);
 /* Codes in range, extremes included (special: all 0 / all max). */
 unsigned max=(1u<<l.bits)-1u;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){unsigned v=special==1?0:special==2?max:next()%(max+1);
  if(l.packed){uint32_t word;memcpy(&word,luma+y*lrow+(x/3)*4,4);word|=v<<(10*(x%3));memcpy(luma+y*lrow+(x/3)*4,&word,4);}
  else if(l.bits==8)luma[y*lrow+x]=(uint8_t)v;else{uint16_t s=(uint16_t)(v<<6|(next()&63));memcpy(luma+y*lrow+2*x,&s,2);}}
 for(unsigned y=0;y<ch;y++)for(unsigned x=0;x<2*cw;x++){unsigned v=special==1?0:special==2?max:next()%(max+1);
  if(l.packed){uint32_t word;memcpy(&word,chroma+y*crow+(x/3)*4,4);word|=v<<(10*(x%3));memcpy(chroma+y*crow+(x/3)*4,&word,4);}
  else if(l.bits==8)chroma[y*crow+x]=(uint8_t)v;else{uint16_t s=(uint16_t)(v<<6|(next()&63));memcpy(chroma+y*crow+2*x,&s,2);}}
 VkFormat lf=l.packed?VK_FORMAT_R32_UINT:l.bits==8?VK_FORMAT_R8_UINT:VK_FORMAT_R16_UINT;
 VkFormat cf=l.packed?VK_FORMAT_R32_UINT:l.bits==8?VK_FORMAT_R8G8_UINT:VK_FORMAT_R16G16_UINT;
 unsigned lw=l.packed?(w+2)/3:w,cwid=l.packed?(2*cw+2)/3:cw,lb=l.packed?4:l.bits==8?1:2,cb=l.packed?4:l.bits==8?2:4;
 struct img tex[3]={image(lf,lw,h,lb,VK_IMAGE_USAGE_SAMPLED_BIT),image(cf,cwid,ch,cb,VK_IMAGE_USAGE_SAMPLED_BIT),
  image(VK_FORMAT_R16_UINT,513,1,2,VK_IMAGE_USAGE_SAMPLED_BIT)};
 /* Tight rows for the upload: the frontend's 64-byte staging rows only pad. */
 uint8_t *lt=malloc((size_t)lw*h*lb),*ct=malloc((size_t)cwid*ch*cb);
 for(unsigned y=0;y<h;y++)memcpy(lt+(size_t)y*lw*lb,luma+y*lrow,(size_t)lw*lb<lrow?(size_t)lw*lb:lrow);
 for(unsigned y=0;y<ch;y++)memcpy(ct+(size_t)y*cwid*cb,chroma+y*crow,(size_t)cwid*cb<crow?(size_t)cwid*cb:crow);
 const void *data[3]={lt,ct,tegu_srgb_knots_8192};upload_all(tex,3,data);
 /* The push block exactly as tegu_shadow_gpu_convert writes it. */
 int k[3][2];tegu_ycbcr_coefficients(l.full_range,l.matrix,k);
 int32_t push[16]={(int32_t)cw,(int32_t)ch,0,0,(int32_t)l.bits,(int32_t)l.packed,(int32_t)l.sub,(int32_t)l.output,
  k[0][0],k[0][1],k[1][0],k[1][1],k[2][0],k[2][1],(int32_t)l.full_range,0};
 unsigned internal=l.output==TEGU_YCBCR_OUT_CONVERTED?90:l.output==TEGU_YCBCR_OUT_SRGB?115:l.bits==8?70:90;
 VkFormat tf=internal==90?VK_FORMAT_A2B10G10R10_UNORM_PACK32:internal==115?VK_FORMAT_R16G16B16A16_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
 unsigned tb=internal==115?8:4;
 struct img target=image(tf,w,h,tb,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
 uint8_t *got=malloc((size_t)w*h*tb);run(ycc_mod,tex,3,push,sizeof(push),&target,got);
 /* The CPU decoder's shadow bytes (tegu_ycbcr_shadow_decode's row path). */
 uint16_t *halves=malloc((size_t)w*8);uint8_t *want=malloc((size_t)w*tb);
 for(unsigned y=0;y<h;y++) {
  int pack10=l.output==TEGU_YCBCR_OUT_CONVERTED;
  tegu_ycbcr_decode_row(&l,luma,lrow,chroma,crow,w,cw,ch,y,pack10?(void *)halves:(void *)want,tegu_srgb_knots_8192);
  if(pack10)tegu_ycbcr_pack10_row(halves,(uint32_t *)want,w);
  const uint8_t *g=got+(size_t)y*w*tb;
  for(unsigned x=0;x<w;x++,texels++)if(memcmp(g+x*tb,want+x*tb,tb)) {
   if(mismatches++<10){uint64_t a=0,b=0;memcpy(&a,g+x*tb,tb);memcpy(&b,want+x*tb,tb);
    fprintf(stderr,"YCBCR fmt=%u full=%u matrix=%u %ux%u at %u,%u gpu=%0*llx cpu=%0*llx\n",format,full,matrix,w,h,x,y,
     (int)tb*2,(unsigned long long)a,(int)tb*2,(unsigned long long)b);}
  }
 }
 free(halves);free(want);free(got);free(lt);free(ct);free(luma);free(chroma);
 for(unsigned i=0;i<3;i++)image_free(&tex[i]);
 image_free(&target);
}
/* tegu_xr_decode_row (render_encoder.inc) over the same tables. */
static void xr_decode_row(unsigned bpp,int srgb,const unsigned char *src,uint16_t *dst,size_t width){
 const uint16_t *colour=srgb?tegu_xr_half_srgb:tegu_xr_half_linear;
 for(size_t x=0;x<width;x++){uint32_t r,g,b;uint16_t alpha=0x3c00u;
  if(bpp==4){uint32_t w;memcpy(&w,src+x*4,4);b=w&1023u;g=(w>>10)&1023u;r=(w>>20)&1023u;}
  else{uint16_t c[4];memcpy(c,src+x*8,8);b=c[0]>>6;g=c[1]>>6;r=c[2]>>6;alpha=tegu_xr_half_alpha[c[3]>>6];}
  dst[x*4+0]=colour[r];dst[x*4+1]=colour[g];dst[x*4+2]=colour[b];dst[x*4+3]=alpha;}
}
static void xr_case(unsigned bpp,int srgb,unsigned w,unsigned h){
 uint8_t *src=malloc((size_t)w*h*bpp);for(size_t i=0;i<(size_t)w*h*bpp;i++)src[i]=(uint8_t)next();
 uint16_t tables[2048];memcpy(tables,srgb?tegu_xr_half_srgb:tegu_xr_half_linear,2048);memcpy(tables+1024,tegu_xr_half_alpha,2048);
 struct img tex[2]={image(bpp==4?VK_FORMAT_R32_UINT:VK_FORMAT_R16G16B16A16_UINT,w,h,bpp,VK_IMAGE_USAGE_SAMPLED_BIT),
  image(VK_FORMAT_R16_UINT,1024,2,2,VK_IMAGE_USAGE_SAMPLED_BIT)};
 const void *data[2]={src,tables};upload_all(tex,2,data);
 int32_t push[4]={bpp==4?0:1,0,0,0};
 struct img target=image(VK_FORMAT_R16G16B16A16_SFLOAT,w,h,8,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
 uint16_t *got=malloc((size_t)w*h*8),*want=malloc((size_t)w*8);run(xr_mod,tex,2,push,sizeof(push),&target,got);
 for(unsigned y=0;y<h;y++){xr_decode_row(bpp,srgb,src+(size_t)y*w*bpp,want,w);
  for(unsigned x=0;x<w;x++,texels++)if(memcmp(got+((size_t)y*w+x)*4,want+x*4,8)&&mismatches++<10)
   fprintf(stderr,"XR bpp=%u srgb=%d at %u,%u gpu=%04x,%04x,%04x,%04x cpu=%04x,%04x,%04x,%04x\n",bpp,srgb,x,y,
    got[((size_t)y*w+x)*4],got[((size_t)y*w+x)*4+1],got[((size_t)y*w+x)*4+2],got[((size_t)y*w+x)*4+3],want[x*4],want[x*4+1],want[x*4+2],want[x*4+3]);}
 free(got);free(want);free(src);
 for(unsigned i=0;i<2;i++)image_free(&tex[i]);
 image_free(&target);
}
int main(int argc,char **argv){
 if(argc!=4){fprintf(stderr,"usage: %s preload.vert.spv ycbcr_convert.frag.spv xr_convert.frag.spv\n",argv[0]);return 2;}
 VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO,0,"convert_shader_vk_test",1,0,0,VK_API_VERSION_1_1};
 VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,0,0,&app,0,0,0,0};VkInstance inst;
 if(vkCreateInstance(&ici,0,&inst)){puts("CONVERT_VK skip: no Vulkan instance");return 77;}
 uint32_t n=1;VkResult r=vkEnumeratePhysicalDevices(inst,&n,&phys);
 if((r&&r!=VK_INCOMPLETE)||!n){puts("CONVERT_VK skip: no Vulkan device");return 77;}
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(phys,&props);
 uint32_t qn=0;vkGetPhysicalDeviceQueueFamilyProperties(phys,&qn,0);VkQueueFamilyProperties qp[16];if(qn>16)qn=16;
 vkGetPhysicalDeviceQueueFamilyProperties(phys,&qn,qp);for(qfam=0;qfam<qn&&!(qp[qfam].queueFlags&VK_QUEUE_GRAPHICS_BIT);qfam++);
 assert(qfam<qn);float prio=1;VkDeviceQueueCreateInfo qci={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,0,0,qfam,1,&prio};
 VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,0,0,1,&qci,0,0,0,0,0};CK(vkCreateDevice(phys,&dci,0,&dev));
 vkGetDeviceQueue(dev,qfam,0,&queue);
 VkCommandPoolCreateInfo pci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,0,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,qfam};CK(vkCreateCommandPool(dev,&pci,0,&pool));
 VkSamplerCreateInfo sci={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sci.magFilter=sci.minFilter=VK_FILTER_NEAREST;
 sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;CK(vkCreateSampler(dev,&sci,0,&sampler));
 vs_mod=module(argv[1]);ycc_mod=module(argv[2]);xr_mod=module(argv[3]);
 printf("CONVERT_VK device=%s\n",props.deviceName);
 /* Every implemented format x range x matrix, odd and even sizes (edge
  * chroma clamps), and extreme codes. */
 static const uint32_t formats[]={500,502,503,505,506,507,508,509,510,520,522,523,525,526,527,528,529,530,540,541,542,543,544,545};
 unsigned ycc=0,xr=0;
 for(unsigned f=0;f<sizeof(formats)/sizeof(formats[0]);f++)
  for(unsigned full=0;full<2;full++)for(unsigned m=0;m<3;m++){
   ycbcr_case(formats[f],full,m,37,23,0);ycc++;
   if(m==0){ycbcr_case(formats[f],full,m,64,16,1);ycbcr_case(formats[f],full,m,63,17,2);ycc+=2;}
  }
 ycbcr_case(500,0,0,1080,64,0);ycbcr_case(500,0,1,1,1,0);ycbcr_case(500,1,1,2,3,0);ycc+=3;
 for(unsigned bpp=4;bpp<=8;bpp+=4)for(int s=0;s<2;s++){xr_case(bpp,s,281,9);xr_case(bpp,s,3,2);xr+=2;}
 printf("CONVERT_VK ycbcr=%u xr=%u texels=%lu mismatches=%lu\n",ycc,xr,texels,mismatches);
 vkDestroySampler(dev,sampler,0);vkDestroyShaderModule(dev,vs_mod,0);vkDestroyShaderModule(dev,ycc_mod,0);vkDestroyShaderModule(dev,xr_mod,0);
 vkDestroyCommandPool(dev,pool,0);vkDestroyDevice(dev,0);vkDestroyInstance(inst,0);
 return mismatches?1:0;
}
