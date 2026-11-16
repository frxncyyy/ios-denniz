/* Decode production packets with unmodified Mesa, check render semantics.
 * No GPU is emulated; pixel correctness remains a device qualification. */
#include "genxml/v9_pack.h"
#include "nord2_metal_render.h"
#include "nord2_preload.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 _Alignas(64) uint8_t data[N2R_BLOCK];
 struct tegu_generic_draw_request q={.width=1125,.height=2436,.row_bytes=4544,.load_action=2,.clear_color=0xff030201,.vertex_count=3,.scissor={11,23,700,1800},.binding_count=1};
 q.bindings[0]=(struct tegu_generic_draw_binding){.kind=TEGU_GENERIC_DRAW_COMBINED,.binding=0,.memory={.token=6,.bytes=4544*2436},.width=1125,.height=2436,.row_bytes=4544,.format=80};
 struct n2r_addresses a={.command=0x1234000000ull,.frame=0x1234000000ull,.heap=0x1280000000ull,.heap_bytes=2<<20,
 .vertex_code=0x12a0000000ull,.fragment_code=0x12b0000000ull,.target=0x12c0000000ull,.binding={0x12d0000000ull}};
 assert(!n2r_build(data,&n2_preload_shader,&q,&a,2,a.command+16384+1024));
 pan_unpack((const struct mali_job_header_packed *)(data+1024),JOB_HEADER,j);
 assert(j.type==MALI_JOB_TYPE_MALLOC_VERTEX&&j.dependency_1==1&&j.next==a.command+16384+1024);
 pan_unpack((const struct mali_primitive_packed *)(data+1056),PRIMITIVE,p);
 assert(p.secondary_shader&&p.index_count==3&&p.draw_mode==MALI_DRAW_MODE_TRIANGLES);
 pan_unpack((const struct mali_draw_packed *)(data+1152),DRAW,d);
 assert(d.vertex_array.packet&&d.blend==a.command+928&&d.blend_count==1&&d.flags_1.render_target_mask==1&&d.flags_1.sample_mask==0xffff);
 assert(d.shader.resources==(a.command+2304+4)&&d.shader.shader==a.command+1600&&d.depth_stencil==a.command+896);
 pan_unpack((const struct mali_shader_environment_packed *)(data+1280),SHADER_ENVIRONMENT,v);
 assert(v.shader==a.command+1536&&v.thread_storage==a.command+832);
 pan_unpack((const struct mali_shader_program_packed *)(data+1536),SHADER_PROGRAM,spd);
 assert(spd.binary==a.vertex_code+n2_preload_shader.position_offset&&spd.stage==MALI_SHADER_STAGE_VERTEX&&spd.preload.r48_r63==n2_preload_shader.vertex_preload>>48);
 /* Mesa81daea6 pan_cmdstream.c: panfrost_upload_shader reserves half the
  * warps for IDVS position programs, and full warps for the varying program.
  * A default-zero (FULL) position descriptor diverges from the arch9 path. */
 assert(spd.vertex_warp_limit==MALI_WARP_LIMIT_HALF);
 pan_unpack((const struct mali_shader_program_packed *)(data+1568),SHADER_PROGRAM,varying);
 assert(varying.vertex_warp_limit==MALI_WARP_LIMIT_FULL&&
        varying.binary==a.vertex_code+n2_preload_shader.secondary_offset&&
        varying.preload.r48_r63==n2_preload_shader.secondary_preload>>48);
 pan_unpack((const struct mali_shader_program_packed *)(data+1600),SHADER_PROGRAM,fragmentShader);
 assert(fragmentShader.stage==MALI_SHADER_STAGE_FRAGMENT&&
        fragmentShader.fragment_coverage_bitmask_type==MALI_COVERAGE_BITMASK_TYPE_GL);
 pan_unpack((const struct mali_framebuffer_parameters_packed *)data,FRAMEBUFFER_PARAMETERS,f);
 assert(f.width==1125&&f.height==2436&&f.bound_max_x==1124&&f.bound_max_y==2435&&f.effective_tile_size==256&&f.color_buffer_allocation==1024);
 pan_unpack((const struct mali_tiler_context_packed *)(data+512),TILER_CONTEXT,t);
 assert(t.heap==a.frame+768&&t.fb_width==1125&&t.fb_height==2436&&!t.polygon_list);
 pan_unpack((const struct mali_tiler_heap_packed *)(data+768),TILER_HEAP,h);
 assert(h.base==a.heap&&h.top==a.heap+a.heap_bytes&&h.bottom==a.heap);
 pan_unpack((const struct mali_fragment_job_payload_packed *)(data+1440),FRAGMENT_JOB_PAYLOAD,frag);
 assert(frag.framebuffer==(a.frame|1)&&frag.bound_max_x==70&&frag.bound_max_y==152);
 pan_unpack((const struct mali_rgb_render_target_packed *)(data+128),RGB_RENDER_TARGET,rt);
 assert(rt.clean_tile_write_enable&&rt.clear.color_3==q.clear_color&&rt.writeback_buffer.base==a.target&&rt.swizzle==0x60a);
 pan_unpack((const struct mali_texture_packed *)(data+4096),TEXTURE,tex);
 assert(tex.width==q.width&&tex.height==q.height&&tex.surfaces==a.command+6144);
 q.load_action=1;assert(!n2r_build(data,&n2_preload_shader,&q,&a,1,0));
 pan_unpack((const struct mali_rgb_render_target_packed *)(data+128),RGB_RENDER_TARGET,load);
 assert(!load.clean_tile_write_enable);
 {
  /* Fragment-job tile bound = draw extent >> 4 (Mesa pan_emit_fragment_job_
   * payload) for Load/DontCare; Clear keeps the full target; the framebuffer
   * bounds stay full (Mesa frame_bounding_box); no extent = identical bytes. */
  uint32_t box[4];assert(n2r_draw_extent(&q,box)&&box[0]==11&&box[1]==23&&box[2]==710&&box[3]==1822);
  _Alignas(64) uint8_t full[N2R_BLOCK],bound[N2R_BLOCK];struct n2r_addresses ea=a;
  assert(!n2r_build(full,&n2_preload_shader,&q,&a,1,0));
  ea.extent_valid=1;memcpy(ea.extent,box,sizeof(box));
  assert(!n2r_build(bound,&n2_preload_shader,&q,&ea,1,0));
  pan_unpack((const struct mali_fragment_job_payload_packed *)(bound+1440),FRAGMENT_JOB_PAYLOAD,bf);
  assert(bf.bound_min_x==0&&bf.bound_min_y==1&&bf.bound_max_x==44&&bf.bound_max_y==113&&bf.framebuffer==(a.frame|1));
  pan_unpack((const struct mali_framebuffer_parameters_packed *)bound,FRAMEBUFFER_PARAMETERS,bfb);
  assert(!bfb.bound_min_x&&!bfb.bound_min_y&&bfb.bound_max_x==1124&&bfb.bound_max_y==2435);
  assert(!memcmp(full,bound,1440)&&!memcmp(full+1448,bound+1448,N2R_BLOCK-1448));
  struct tegu_generic_draw_request dq=q;dq.load_action=0;
  assert(!n2r_build(bound,&n2_preload_shader,&dq,&ea,1,0));
  pan_unpack((const struct mali_fragment_job_payload_packed *)(bound+1440),FRAGMENT_JOB_PAYLOAD,df);
  assert(df.bound_min_y==1&&df.bound_max_x==44);
  dq.load_action=2;assert(!n2r_build(bound,&n2_preload_shader,&dq,&ea,1,0));
  pan_unpack((const struct mali_fragment_job_payload_packed *)(bound+1440),FRAGMENT_JOB_PAYLOAD,cf);
  assert(!cf.bound_min_x&&!cf.bound_min_y&&cf.bound_max_x==70&&cf.bound_max_y==152);
  ea.extent[2]=q.width;assert(n2r_build(bound,&n2_preload_shader,&q,&ea,1,0));
  ea.extent[2]=5;assert(n2r_build(bound,&n2_preload_shader,&q,&ea,1,0));
  ea.extent[2]=710;ea.extent_valid=2;assert(n2r_build(bound,&n2_preload_shader,&q,&ea,1,0));
  /* Viewport clamps the box like the tiler scissor; a clipped draw has none. */
  struct tegu_generic_draw_request vq=q;vq.viewport_valid=1;
  vq.viewport[0]=100;vq.viewport[1]=-50;vq.viewport[2]=5000;vq.viewport[3]=300;vq.viewport[4]=0;vq.viewport[5]=1;
  assert(n2r_draw_extent(&vq,box)&&box[0]==100&&box[1]==23&&box[2]==710&&box[3]==249);
  vq.viewport[0]=900;assert(!n2r_draw_extent(&vq,box));
 }
 q.scissor[2]=q.width;assert(n2r_build(data,&n2_preload_shader,&q,&a,1,0));q.scissor[2]=700;
 q.bindings[0].memory.bytes--;assert(n2r_build(data,&n2_preload_shader,&q,&a,1,0));
 q.bindings[0].memory.bytes++;q.load_action=2;
 const unsigned formats[]={10,70,80,115};
 for(unsigned i=0;i<4;i++){
  q.target_format=formats[i];q.row_bytes=(q.width*n2r_bpp(formats[i])+63)&~63u;
  const double color[4]={.25,.5,.75,1};assert(!n2r_clear(formats[i],color,q.clear_words));
  assert(!n2r_build(data,&n2_preload_shader,&q,&a,1,0));
  pan_unpack((const struct mali_rgb_render_target_packed *)(data+128),RGB_RENDER_TARGET,colorRT);
  pan_unpack((const struct mali_framebuffer_parameters_packed *)data,FRAMEBUFFER_PARAMETERS,colorFB);
  pan_unpack((const struct mali_blend_packed *)(data+928),BLEND,colorBlend);
  assert(colorRT.writeback_buffer.row_stride==q.row_bytes);
  assert(colorRT.swizzle==(formats[i]==80?0x60a:0x688));
  if(formats[i]==115){
   assert(colorRT.internal_format==MALI_COLOR_BUFFER_INTERNAL_FORMAT_RGBA16F);
   assert(colorRT.writeback_float_format==MALI_FLOAT_COLOR_FORMAT_RGBA16F);
   assert(colorBlend.internal.fixed_function.conversion.memory_format==(MALI_RGBA16F<<12));
   assert(colorFB.color_buffer_allocation==2048);
   assert(q.clear_words[0]==0x38003400&&q.clear_words[1]==0x3c003a00);
  }else{
   assert(colorRT.internal_format==MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8);
   assert(colorRT.writeback_format==(formats[i]==10?MALI_COLOR_FORMAT_R8:MALI_COLOR_FORMAT_R8G8B8A8));
  }
  q.row_bytes=0;assert(n2r_build(data,&n2_preload_shader,&q,&a,1,0));
 }
 q.target_format=80;q.row_bytes=4544;q.viewport_valid=1;
 const float view[6]={13,19,500,700,.2f,.8f};memcpy(q.viewport,view,sizeof(view));
 assert(!n2r_build(data,&n2_preload_shader,&q,&a,1,0));
 float transform[8];memcpy(transform,data+1664,sizeof(transform));
 assert(transform[0]==250&&transform[1]==-350&&transform[4]==263&&transform[5]==369&&transform[6]==.2f);
 q.viewport[2]=0;assert(n2r_build(data,&n2_preload_shader,&q,&a,1,0));q.viewport_valid=0;
 struct tegu_generic_draw_shader masked=n2_preload_shader;masked.blend.write_mask=8; /* MTLColorWriteMaskRed -> Mali R bit0 */
 assert(!n2r_build(data,&masked,&q,&a,1,0));
 pan_unpack((const struct mali_blend_packed *)(data+928),BLEND,maskedBlend);
 assert(maskedBlend.equation.color_mask==1&&maskedBlend.load_destination);
 q.target_format=551;assert(n2r_build(data,&n2_preload_shader,&q,&a,1,0));
 {
  /* Native indexed attribute fetch (pinned Mesa gallium v9: no index scan,
   * BUFFER.size = bound range, base_vertex_offset = 0 here). */
  _Alignas(64) uint8_t ix[N2R_BLOCK];
  struct tegu_generic_draw_shader as=n2_preload_shader;as.attribute_count=1;
  as.attributes[0]=(struct tegu_vertex_attribute){.location=0,.buffer_index=0,.format=103,.offset=8,.stride=16,.input_rate=0,.divisor=1};
  struct tegu_generic_draw_request iq={.target_format=80,.width=64,.height=64,.row_bytes=256,.load_action=2,.vertex_count=6,
   .primitive=TEGU_GENERIC_DRAW_TRIANGLE_STRIP,.index_size=2,.scissor={0,0,64,64},.binding_count=1};
  iq.bindings[0]=q.bindings[0];iq.bindings[0].memory.bytes=4544*2436;
  iq.vertex=(struct tegu_generic_draw_memory){.token=7,.bytes=64};   /* 4 vertices for 6 indices */
  iq.index=(struct tegu_generic_draw_memory){.token=8,.bytes=12};
  struct n2r_addresses ia=a;ia.vertex=0x12f0000000ull;ia.index=0x1300000000ull;
  assert(!n2r_build(ix,&as,&iq,&ia,1,0));
  pan_unpack((const struct mali_primitive_packed *)(ix+1056),PRIMITIVE,ip);
  assert(ip.index_type==MALI_INDEX_TYPE_UINT16&&ip.index_count==6&&ip.primitive_restart&&!ip.base_vertex_offset);
  pan_unpack((const struct mali_buffer_packed *)(ix+2368+17*32),BUFFER,vb);
  assert(vb.address==ia.vertex&&vb.size==64);
  iq.primitive=TEGU_GENERIC_DRAW_TRIANGLES;assert(!n2r_build(ix,&as,&iq,&ia,1,0));
  pan_unpack((const struct mali_primitive_packed *)(ix+1056),PRIMITIVE,lp);assert(!lp.primitive_restart);
  iq.vertex.bytes=8;assert(n2r_build(ix,&as,&iq,&ia,1,0));iq.vertex.bytes=64;  /* vertex 0 must fit */
  iq.index.bytes=10;assert(n2r_build(ix,&as,&iq,&ia,1,0));iq.index.bytes=12;    /* every index must fit */
  struct n2r_draw_options bv={.version=N2R_DRAW_OPTIONS_VERSION,.flags=N2R_DRAW_WRITE_VERTEX_BASE,.vertex_base=2};
  struct tegu_generic_draw_shader asf=as;asf.vertex_fau_words=17;
  assert(n2r_build_v3(ix,&asf,&iq,&ia,NULL,&bv,1,0));                         /* baseVertex!=0 refused */
  bv.vertex_base=0;assert(!n2r_build_v3(ix,&asf,&iq,&ia,NULL,&bv,1,0));
  {/* Face culling: Metal Back+CCW -> DRAW cull_back_face + front_face_ccw;
     * lines never cull; unknown option bits refuse. */
   struct n2r_draw_options co={.version=N2R_DRAW_OPTIONS_VERSION,.flags=N2R_DRAW_CULL_BACK|N2R_DRAW_FRONT_CCW};
   assert(!n2r_build_v3(ix,&as,&iq,&ia,NULL,&co,1,0));
   pan_unpack((const struct mali_draw_packed *)(ix+1152),DRAW,cd);
   assert(cd.flags_0.cull_back_face&&!cd.flags_0.cull_front_face&&cd.flags_0.front_face_ccw);
   co.flags=N2R_DRAW_CULL_FRONT;assert(!n2r_build_v3(ix,&as,&iq,&ia,NULL,&co,1,0));
   pan_unpack((const struct mali_draw_packed *)(ix+1152),DRAW,cf);
   assert(cf.flags_0.cull_front_face&&!cf.flags_0.cull_back_face&&!cf.flags_0.front_face_ccw);
   struct tegu_generic_draw_request lq=iq;lq.primitive=TEGU_GENERIC_DRAW_LINES;lq.index_size=0;lq.index.token=0;lq.index.bytes=0;lq.vertex_count=4;
   if(!n2r_build_v3(ix,&as,&lq,&ia,NULL,&co,1,0)){pan_unpack((const struct mali_draw_packed *)(ix+1152),DRAW,cl);
    assert(!cl.flags_0.cull_front_face&&!cl.flags_0.cull_back_face);}
   co.flags=1024;assert(n2r_build_v3(ix,&as,&iq,&ia,NULL,&co,1,0));
   _Alignas(64) uint8_t nc[N2R_BLOCK];assert(!n2r_build(nc,&as,&iq,&ia,1,0));
   pan_unpack((const struct mali_draw_packed *)(nc+1152),DRAW,nd);assert(!nd.flags_0.cull_back_face&&!nd.flags_0.front_face_ccw);
  }
  {/* Blend colour (Metal 11..14 -> MALI_BLEND_OPERAND_C_CONSTANT, scalar
     * BLEND.blend_constant, UNORM8 MSB-aligned per Mesa pan_pack_blend_
     * constant) and depth clip mode Clamp (depth_cull off, clamp BOUNDS). */
   struct tegu_generic_draw_shader cs=as;
   cs.blend=(struct tegu_blend_inputs){1,1,0,11,12,0,13,14,15}; /* c*src + (1-c)*dst */
   struct n2r_draw_options bo={.version=N2R_DRAW_OPTIONS_VERSION,.flags=N2R_DRAW_BLEND_CONSTANT|N2R_DRAW_DEPTH_CLAMP,
    .blend_constant={.5f,.5f,.5f,.5f}};
   assert(!n2r_build_v3(ix,&cs,&iq,&ia,NULL,&bo,1,0));
   pan_unpack((const struct mali_blend_packed *)(ix+928),BLEND,cb);
   assert(cb.blend_constant==(uint32_t)(.5f*65280.0f)&&cb.equation.rgb.c==MALI_BLEND_OPERAND_C_CONSTANT&&
          cb.equation.alpha.c==MALI_BLEND_OPERAND_C_CONSTANT&&cb.load_destination);
   pan_unpack((const struct mali_depth_stencil_packed *)(ix+896),DEPTH_STENCIL,cds);
   assert(!cds.depth_cull_enable&&cds.depth_clamp_mode==MALI_DEPTH_CLAMP_MODE_BOUNDS);
   bo.blend_constant[3]=.25f;assert(n2r_build_v3(ix,&cs,&iq,&ia,NULL,&bo,1,0)); /* not homogeneous: one scalar */
   bo.flags=0;assert(!n2r_build_v3(ix,&cs,&iq,&ia,NULL,&bo,1,0));             /* Metal default colour 0 */
   pan_unpack((const struct mali_blend_packed *)(ix+928),BLEND,cz);
   pan_unpack((const struct mali_depth_stencil_packed *)(ix+896),DEPTH_STENCIL,dz);
   assert(cz.blend_constant==0&&dz.depth_cull_enable&&dz.depth_clamp_mode==MALI_DEPTH_CLAMP_MODE_0_1);
   cs.blend=(struct tegu_blend_inputs){1,1,0,10,0,0,1,0,15};assert(n2r_build_v3(ix,&cs,&iq,&ia,NULL,&bo,1,0)); /* SourceAlphaSaturated */
   struct tegu_generic_draw_request fq=iq;fq.target_format=115;fq.row_bytes=512;
   cs.blend=(struct tegu_blend_inputs){1,1,0,11,0,0,1,0,15};assert(n2r_build_v3(ix,&cs,&fq,&ia,NULL,&bo,1,0)); /* float target */
  }
  {/* Tile-buffer depth/stencil: FB z format/clears, DEPTH_STENCIL from the
     * Metal state (compare numbering, stencil op remap, bias x2 for unorm). */
   struct n2r_depth_stencil zs={.version=N2R_DEPTH_STENCIL_VERSION,.format=255,.load=2,.depth_compare=1,.depth_write=1,.stencil_enable=1,
    .clear_depth=.75f,.clear_stencil=3,.front={3,2,6,1,0xff,0x0f,9},.back={7,0,0,0,0xff,0,9},.bias_units=1.5f,.bias_slope=2,.bias_clamp=0};
   struct n2r_draw_options zo={.version=N2R_DRAW_OPTIONS_VERSION,.flags=N2R_DRAW_DEPTH_STENCIL,.zs=&zs};
   assert(!n2r_build_v3(ix,&as,&iq,&ia,NULL,&zo,1,0));
   pan_unpack((const struct mali_framebuffer_parameters_packed *)ix,FRAMEBUFFER_PARAMETERS,zf);
   assert(zf.z_internal_format==MALI_Z_INTERNAL_FORMAT_D24&&zf.z_clear==.75f&&zf.s_clear==3&&!zf.z_write_enable&&!zf.s_write_enable);
   pan_unpack((const struct mali_depth_stencil_packed *)(ix+896),DEPTH_STENCIL,zd);
   assert(zd.depth_function==MALI_FUNC_LESS&&zd.depth_write_enable&&zd.stencil_test_enable&&zd.front_compare_function==MALI_FUNC_LEQUAL&&
          zd.front_stencil_fail==MALI_STENCIL_OP_REPLACE&&zd.front_depth_fail==MALI_STENCIL_OP_INCR_WRAP&&zd.front_depth_pass==MALI_STENCIL_OP_ZERO&&
          zd.front_write_mask==0x0f&&zd.front_reference_value==9&&zd.back_compare_function==MALI_FUNC_ALWAYS&&
          zd.depth_bias_enable&&zd.depth_units==3.0f&&zd.depth_factor==2.0f);
   zs.format=252;zs.stencil_enable=0;assert(!n2r_build_v3(ix,&as,&iq,&ia,NULL,&zo,1,0));
   pan_unpack((const struct mali_framebuffer_parameters_packed *)ix,FRAMEBUFFER_PARAMETERS,zf2);
   pan_unpack((const struct mali_depth_stencil_packed *)(ix+896),DEPTH_STENCIL,zd2);
   assert(zf2.z_internal_format==MALI_Z_INTERNAL_FORMAT_D32&&zd2.depth_units==1.5f&&!zd2.stencil_test_enable);
   zs.stencil_enable=1;assert(n2r_build_v3(ix,&as,&iq,&ia,NULL,&zo,1,0));zs.stencil_enable=0;   /* no stencil plane */
   zs.format=253;assert(n2r_build_v3(ix,&as,&iq,&ia,NULL,&zo,1,0));zs.format=252;               /* S8 cannot test depth */
   zs.load=3;assert(n2r_build_v3(ix,&as,&iq,&ia,NULL,&zo,1,0));zs.load=2;
   zo.zs=NULL;assert(n2r_build_v3(ix,&as,&iq,&ia,NULL,&zo,1,0));
  }
  puts("PASS native indexed attribute draws: index type/count, strip restart, bounds, baseVertex refusal, face culling, blend colour, depth clamp, depth/stencil");
 }
 puts("PASS R8/RGBA8/BGRA8/RGBA16F format, clear, blend and row-stride oracle");
 {
  /* Pre-frame colour preload (Mesa 81daea66 pan_fb_preload.c v9 DCD,
   * pan_desc.c pre_frame_0, pan_fix_frame_shader_mode). */
  _Alignas(64) uint8_t owner[N2R_BLOCK],pre[N2R_BLOCK];
  struct tegu_generic_draw_request lq=q;lq.target_format=80;lq.row_bytes=4544;lq.load_action=1;lq.viewport_valid=0;
  struct n2r_addresses oa=a;oa.frame_shader_dcds=a.command+N2R_BLOCK+N2R_FRAME_DCDS;
  assert(!n2r_build(owner,&n2_preload_shader,&lq,&oa,1,0));
  pan_unpack((const struct mali_framebuffer_parameters_packed *)owner,FRAMEBUFFER_PARAMETERS,ofb);
  assert(ofb.pre_frame_0==MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT&&ofb.pre_frame_1==MALI_PRE_POST_FRAME_SHADER_MODE_NEVER&&
         ofb.post_frame==MALI_PRE_POST_FRAME_SHADER_MODE_NEVER&&ofb.frame_shader_dcds==oa.frame_shader_dcds);
  pan_unpack((const struct mali_rgb_render_target_packed *)(owner+128),RGB_RENDER_TARGET,ort);
  assert(!ort.clean_tile_write_enable);
  pan_unpack((const struct mali_job_header_packed *)(owner+1024),JOB_HEADER,oj);
  assert(oj.type==MALI_JOB_TYPE_MALLOC_VERTEX&&oj.index==1&&!oj.dependency_1&&!oj.next);
  /* Without an array the packet is byte-identical to the historical one. */
  _Alignas(64) uint8_t plain[N2R_BLOCK];struct n2r_addresses na=oa;na.frame_shader_dcds=0;
  assert(!n2r_build(plain,&n2_preload_shader,&lq,&na,1,0));
  pan_unpack((const struct mali_framebuffer_parameters_packed *)plain,FRAMEBUFFER_PARAMETERS,pfb);
  assert(pfb.pre_frame_0==MALI_PRE_POST_FRAME_SHADER_MODE_NEVER&&!pfb.frame_shader_dcds);
  assert(!memcmp(plain+32,owner+32,N2R_BLOCK-32));
  /* A forced clean tile write (CLEAR/DontCare) degrades INTERSECT to ALWAYS. */
  struct tegu_generic_draw_request cq=lq;cq.load_action=2;
  assert(!n2r_build(owner,&n2_preload_shader,&cq,&oa,1,0));
  pan_unpack((const struct mali_framebuffer_parameters_packed *)owner,FRAMEBUFFER_PARAMETERS,cfb);
  assert(cfb.pre_frame_0==MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS);
  /* DontCare (0) is not a clean-tile write: untouched tiles keep memory. */
  cq.load_action=0;assert(!n2r_build(owner,&n2_preload_shader,&cq,&oa,1,0));
  pan_unpack((const struct mali_framebuffer_parameters_packed *)owner,FRAMEBUFFER_PARAMETERS,dfb);
  pan_unpack((const struct mali_rgb_render_target_packed *)(owner+128),RGB_RENDER_TARGET,drt);
  assert(dfb.pre_frame_0==MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT&&!drt.clean_tile_write_enable);
  struct n2r_addresses bad=oa;bad.frame_shader_dcds|=32;assert(n2r_build(owner,&n2_preload_shader,&lq,&bad,1,0));
  /* The frame block: no job, DCD0 colour preload, DCD1/DCD2 zero. */
  struct n2r_addresses pa=a;pa.command=a.command+N2R_BLOCK;pa.vertex_code=0;pa.fragment_code=0x12e0000000ull;
  struct tegu_generic_draw_request pq={.target_format=80,.width=1125,.height=2436,.row_bytes=4544,.load_action=1,.vertex_count=3,.scissor={0,0,1125,2436},.binding_count=1};
  pq.bindings[0]=(struct tegu_generic_draw_binding){.kind=TEGU_GENERIC_DRAW_COMBINED,.binding=0,.memory={.token=6,.bytes=4544*2436},.width=1125,.height=2436,.row_bytes=4544,.format=80};
  memset(pre,0xa5,sizeof(pre));
  assert(!n2r_build_frame_preload(pre,&n2_frame_preload_shader,&pq,&pa));
  static const uint8_t zero[384];
  assert(!memcmp(pre+1024,zero,128)&&!memcmp(pre+N2R_FRAME_DCDS+128,zero,256));
  assert(!memcmp(pre+1536,zero,64)); /* no vertex/varying programs */
  pan_unpack((const struct mali_draw_packed *)(pre+N2R_FRAME_DCDS),DRAW,dcd);
  assert(!dcd.vertex_array.packet&&!dcd.vertex_array.pointer&&dcd.flags_0.clean_fragment_write&&
         dcd.flags_0.allow_forward_pixel_to_kill&&dcd.flags_0.allow_forward_pixel_to_be_killed&&
         dcd.flags_0.pixel_kill_operation==MALI_PIXEL_KILL_FORCE_LATE&&dcd.flags_0.zs_update_operation==MALI_PIXEL_KILL_FORCE_LATE&&
         !dcd.flags_0.multisample_enable&&dcd.flags_1.sample_mask==0xffff&&dcd.flags_1.render_target_mask==1&&
         dcd.blend==pa.command+928&&dcd.blend_count==1&&dcd.depth_stencil==pa.command+896&&dcd.maximum_z==1.0f&&
         dcd.shader.resources==(pa.command+2304+4)&&dcd.shader.shader==pa.command+1600&&
         dcd.shader.thread_storage==a.frame+832&&dcd.shader.fau==pa.command+1920&&
         dcd.shader.fau_count==(n2_frame_preload_shader.fragment_fau_words+1)/2);
  pan_unpack((const struct mali_shader_program_packed *)(pre+1600),SHADER_PROGRAM,fspd);
  assert(fspd.stage==MALI_SHADER_STAGE_FRAGMENT&&fspd.binary==pa.fragment_code&&
         fspd.preload.r48_r63==n2_frame_preload_shader.fragment_preload>>48);
  /* r59 = BI_PRELOAD_POSITION_XY: the program addresses texels by pixel. */
  assert(n2_frame_preload_shader.fragment_preload&(1ull<<59));
  pan_unpack((const struct mali_texture_packed *)(pre+4096),TEXTURE,ftex);
  assert(ftex.width==1125&&ftex.height==2436);
  /* Refusals: frame blocks are single-target LOAD, unchained, unnested. */
  pq.load_action=2;assert(n2r_build_frame_preload(pre,&n2_frame_preload_shader,&pq,&pa));pq.load_action=1;
  struct n2r_addresses nest=pa;nest.frame_shader_dcds=oa.frame_shader_dcds;assert(n2r_build_frame_preload(pre,&n2_frame_preload_shader,&pq,&nest));
  struct n2r_addresses nofs=pa;nofs.fragment_code=0;assert(n2r_build_frame_preload(pre,&n2_frame_preload_shader,&pq,&nofs));
  {/* True-MRT LOAD: per-target frame preload (rt 1 of RGBA8/RGBA16F) and
    * the owner's pre_frame_0/1 from frame_shader_mask. */
   struct n2r_render_target mts[2]={
    {.format=80,.load_action=1,.write_mask=0,.address=0x12c0000000ull,.row_bytes=4544,.width=1125,.height=2436},
    {.format=115,.load_action=1,.write_mask=15,.address=0x12c1000000ull,.row_bytes=9024,.width=1125,.height=2436}};
   struct n2r_render_targets mrts={.version=N2R_RENDER_TARGETS_VERSION,.count=2,.targets=mts};
   struct tegu_generic_draw_request mq=pq;mq.target_format=115;mq.row_bytes=9024;
   mq.bindings[0].format=115;mq.bindings[0].row_bytes=9024;mq.bindings[0].memory.bytes=9024ull*2436;
   struct n2r_addresses ma=pa;ma.binding[0]=mts[1].address;ma.target=mts[1].address;
   memset(pre,0xa5,sizeof(pre));
   assert(!n2r_build_frame_preload_rt(pre,&n2_frame_preload_shader,&mq,&ma,&mrts,1));
   pan_unpack((const struct mali_draw_packed *)(pre+N2R_FRAME_DCDS),DRAW,mdcd);
   assert(mdcd.blend_count==2&&mdcd.flags_1.render_target_mask==2&&mdcd.flags_0.clean_fragment_write&&
          mdcd.blend==ma.command+928&&mdcd.shader.fau==ma.command+1920&&mdcd.shader.thread_storage==a.frame+832);
   assert(!memcmp(pre+N2R_FRAME_DCDS+128,zero,256));
   uint32_t fw;memcpy(&fw,pre+1920+48,4);assert(((fw>>16)&15)==1);
   uint32_t b1w2;memcpy(&b1w2,pre+928+16+8,4);assert((b1w2&~0x000f0000u)==(fw&~0x000f0000u));
   pan_unpack((const struct mali_texture_packed *)(pre+4096),TEXTURE,mtex);
   assert(mtex.width==1125&&mtex.height==2436);
   assert(n2r_build_frame_preload_rt(pre,&n2_frame_preload_shader,&mq,&ma,&mrts,2)); /* no such target */
   assert(n2r_build_frame_preload_rt(pre,&n2_frame_preload_shader,&mq,&ma,NULL,0));
   mts[1].load_action=2;assert(n2r_build_frame_preload_rt(pre,&n2_frame_preload_shader,&mq,&ma,&mrts,1));mts[1].load_action=1;
   /* Owner: both modes from the mask; a mask without an array refuses. */
   _Alignas(64) uint8_t own[N2R_BLOCK];struct tegu_generic_draw_request oq=lq;oq.target_format=80;oq.load_action=1;
   struct n2r_addresses moa=a;moa.frame_shader_dcds=a.command+N2R_BLOCK+N2R_FRAME_DCDS;moa.frame_shader_mask=3;
   mts[0].write_mask=15;
   assert(!n2r_build_v4(own,&n2_preload_shader,&oq,&moa,NULL,NULL,&mrts,1,0));
   pan_unpack((const struct mali_framebuffer_parameters_packed *)own,FRAMEBUFFER_PARAMETERS,mfb);
   assert(mfb.pre_frame_0==MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT&&mfb.pre_frame_1==MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT&&
          mfb.post_frame==MALI_PRE_POST_FRAME_SHADER_MODE_NEVER&&mfb.render_target_count==2);
   {/* Discarded target (Metal store DontCare): no writeback, no clean
     * writes, and its Clear neither forces ALWAYS nor blocks tile bounding. */
    struct n2r_render_target dt[2];memcpy(dt,mts,sizeof(dt));dt[1].load_action=2;dt[1].reserved=N2R_TARGET_DISCARD;
    struct n2r_render_targets drt={.version=N2R_RENDER_TARGETS_VERSION,.count=2,.targets=dt};
    struct n2r_addresses da=moa;da.frame_shader_mask=1;da.extent_valid=1;da.extent[0]=16;da.extent[1]=32;da.extent[2]=100;da.extent[3]=200;
    dt[0].load_action=1;
    assert(!n2r_build_v4(own,&n2_preload_shader,&oq,&da,NULL,NULL,&drt,1,0));
    pan_unpack((const struct mali_rgb_render_target_packed *)(own+128+64),RGB_RENDER_TARGET,d1);
    assert(!d1.write_enable&&!d1.clean_tile_write_enable);
    pan_unpack((const struct mali_framebuffer_parameters_packed *)own,FRAMEBUFFER_PARAMETERS,dfb2);
    assert(dfb2.pre_frame_0==MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT);
    pan_unpack((const struct mali_fragment_job_payload_packed *)(own+1440),FRAGMENT_JOB_PAYLOAD,dfj);
    assert(dfj.bound_min_x==1&&dfj.bound_min_y==2);
    dt[1].reserved=2;assert(n2r_build_v4(own,&n2_preload_shader,&oq,&da,NULL,NULL,&drt,1,0));}
   {/* MULTI_TARGET contract: FAU 48+8*rt = target rt's internal blend word,
     * fixed_function.rt=rt; rt1 is RGBA16F (blend format 223). */
    uint32_t f0[2],f1[2];memcpy(f0,own+1920+48,8);memcpy(f1,own+1920+56,8);
    assert(((f0[0]>>16)&15)==0&&((f1[0]>>16)&15)==1&&(f1[1]>>12)==223u&&f0[1]!=f1[1]);}
   moa.frame_shader_mask=2;assert(!n2r_build_v4(own,&n2_preload_shader,&oq,&moa,NULL,NULL,&mrts,1,0));
   pan_unpack((const struct mali_framebuffer_parameters_packed *)own,FRAMEBUFFER_PARAMETERS,m2fb);
   assert(m2fb.pre_frame_0==MALI_PRE_POST_FRAME_SHADER_MODE_NEVER&&m2fb.pre_frame_1==MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT);
   moa.frame_shader_mask=4;assert(n2r_build_v4(own,&n2_preload_shader,&oq,&moa,NULL,NULL,&mrts,1,0));
   moa.frame_shader_mask=1;moa.frame_shader_dcds=0;assert(n2r_build_v4(own,&n2_preload_shader,&oq,&moa,NULL,NULL,&mrts,1,0));
   /* Tile-buffer budget: 2 x RGBA16F at 4x = 16384 bytes fits; 3 x
    * RGBA16F at 4x (24576) is refused rather than tiling at 16x16. */
   {struct n2r_render_target bt[3];uint32_t bs[3]={4,4,4};
    for(unsigned i=0;i<3;i++)bt[i]=(struct n2r_render_target){.format=115,.load_action=2,.write_mask=15,
     .address=0x1300000000ull+0x4000000ull*i,.row_bytes=9024,.width=1125,.height=2436};
    struct n2r_render_targets brt={.version=N2R_RENDER_TARGETS_MSAA_VERSION,.count=2,.targets=bt,.sample_counts=bs};
    struct n2r_addresses ba=a;struct tegu_generic_draw_request bq=oq;bq.load_action=2;
    assert(!n2r_build_v4(own,&n2_preload_shader,&bq,&ba,NULL,NULL,&brt,1,0));
    pan_unpack((const struct mali_framebuffer_parameters_packed *)own,FRAMEBUFFER_PARAMETERS,bfb4);
    assert(bfb4.color_buffer_allocation==16384&&bfb4.sample_count==4);
    brt.count=3;brt.sample_locations=a.command+12288;assert(n2r_build_v4(own,&n2_preload_shader,&bq,&ba,NULL,NULL,&brt,1,0));}
  }
  puts("PASS pre-frame preload oracle: pre_frame_0 INTERSECT/ALWAYS, frame_shader_dcds, colour DCD, no preload job");
 }
 puts("PASS G77 graphics semantic oracle:64-bit VAs,secondary stage,frame/RT,heap descriptor,tile bounds,LOAD preservation,texture and refusal");
}
