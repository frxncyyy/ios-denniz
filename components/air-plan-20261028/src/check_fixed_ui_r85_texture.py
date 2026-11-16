#!/usr/bin/env python3
import argparse,ctypes as C,hashlib,re,json
from pathlib import Path
root=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description='Reproduce and verify the exact R85 sampled fixed-UI AIR bridge contract')
parser.add_argument('--bridge',type=Path,required=True)
args=parser.parse_args()
lib=C.CDLL(str(args.bridge.resolve()))
class Blob(C.Structure): _fields_=[('bytes',C.POINTER(C.c_ubyte)),('length',C.c_size_t)]
class FC(C.Structure): _fields_=[('kind',C.c_uint32),('type',C.c_uint32),('index',C.c_uint32),('size',C.c_uint32),('name',C.POINTER(C.c_ubyte)),('name_len',C.c_size_t),('data',C.c_ubyte*64)]
class Attr(C.Structure): _fields_=[('index',C.c_uint32),('format',C.c_uint32),('offset',C.c_uint32),('buffer_index',C.c_uint32)]
class Layout(C.Structure): _fields_=[('index',C.c_uint32),('stride',C.c_uint32),('step_function',C.c_uint32),('step_rate',C.c_uint32)]
class Res(C.Structure): _fields_=[('kind',C.c_uint32),('stage',C.c_uint32),('index',C.c_uint32),('format',C.c_uint32),('texture_type',C.c_uint32),('width',C.c_uint32),('height',C.c_uint32),('array_length',C.c_uint32),('mipmap_levels',C.c_uint32),('sample_count',C.c_uint32),('usage',C.c_uint32),('sparse',C.c_uint32),('depth_twin',C.c_uint32),('swizzle',C.c_uint32*4),('parent_level',C.c_uint32),('parent_width',C.c_uint32),('parent_height',C.c_uint32),('parent_depth',C.c_uint32),('own_format_views',C.c_uint32),('address_mode_identity',C.c_uint32),('length',C.c_uint64),('offset',C.c_uint64),('gpu_address',C.c_uint64),('resource_id',C.c_uint64),('stride',C.c_uint32),('reserved',C.c_uint32),('min_filter',C.c_uint32),('mag_filter',C.c_uint32),('mip_filter',C.c_uint32),('address_s',C.c_uint32),('address_t',C.c_uint32),('address_r',C.c_uint32),('border_color',C.c_uint32),('normalized_coordinates',C.c_uint32),('lod_average',C.c_uint32),('max_anisotropy',C.c_uint32),('compare_function',C.c_uint32),('reduction_mode',C.c_uint32),('lod_min_clamp',C.c_float),('lod_max_clamp',C.c_float)]
class Draw(C.Structure): _fields_=[('version',C.c_uint32),('vertex_start',C.c_uint32),('vertex_count',C.c_uint32),('instance_count',C.c_uint32),('base_instance',C.c_uint32)]
class Use(C.Structure): _fields_=[('kind',C.c_uint32),('stage',C.c_uint32),('index',C.c_uint32),('class_mask',C.c_uint32),('flags',C.c_uint32),('reserved',C.c_uint32),('range_offset',C.c_uint64),('range_length',C.c_uint64)]
class Att(C.Structure): _fields_=[('format',C.c_uint32),('write_mask',C.c_uint32)]
class Typed(C.Structure): _fields_=[('version',C.c_uint32),('count',C.c_uint32),('attachment_count',C.c_uint32),('diagnostic_count',C.c_uint32),('neutral_count',C.c_uint32),('reserved',C.c_uint32*3),('attachments',Att*8),('uses',Use*186)]
class FixedBinding(C.Structure): _fields_=[('stage',C.c_uint32),('index',C.c_uint32),('descriptor_set',C.c_uint32),('binding',C.c_uint32),('bind_offset',C.c_uint64),('resource_length',C.c_uint64),('descriptor_offset',C.c_uint64),('descriptor_length',C.c_uint64)]
class PushSpan(C.Structure): _fields_=[('stage',C.c_uint32),('offset',C.c_uint32),('length',C.c_uint32),('bytes_offset',C.c_uint32)]
class TextureBinding(C.Structure): _fields_=[('texture_stage',C.c_uint32),('texture_index',C.c_uint32),('sampler_stage',C.c_uint32),('sampler_index',C.c_uint32),('descriptor_set',C.c_uint32),('binding',C.c_uint32),('format',C.c_uint32),('texture_type',C.c_uint32),('width',C.c_uint32),('height',C.c_uint32),('resource_id',C.c_uint64)]
class FixedPlan(C.Structure): _fields_=[('version',C.c_uint32),('binding_count',C.c_uint32),('push_count',C.c_uint32),('push_bytes_length',C.c_uint32),('constant_stage',C.c_uint32),('constant_length',C.c_uint32),('specialization_id',C.c_uint32),('specialization_length',C.c_uint32),('bindings',FixedBinding*4),('texture',TextureBinding),('pushes',PushSpan*2),('push_bytes',C.c_ubyte*20),('constant_bytes',C.c_ubyte*8),('specialization_bytes',C.c_ubyte*4)]
lib.tegu_air_library_open.restype=C.c_void_p;lib.tegu_air_library_open.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p,C.c_size_t]
lib.tegu_air_library_specialize.restype=C.c_void_p;lib.tegu_air_library_specialize.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(FC),C.c_size_t,C.c_char_p,C.c_size_t]
lib.tegu_air_render_pipeline_create_with_vertex_descriptor.restype=C.c_void_p
lib.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,C.c_size_t,C.c_uint32,C.POINTER(Attr),C.c_size_t,C.POINTER(Layout),C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Blob),C.POINTER(Blob),C.POINTER(Blob),C.c_char_p,C.c_size_t]
lib.tegu_air_render_pipeline_resource_contract.argtypes=[C.c_void_p,C.POINTER(Blob),C.c_char_p,C.c_size_t]
lib.tegu_air_render_pipeline_resource_contract.restype=C.c_int32
lib.tegu_air_render_pipeline_plan_draw_typed.argtypes=[C.c_void_p,C.POINTER(Res),C.c_size_t,C.POINTER(Draw),C.POINTER(Blob),C.POINTER(Typed),C.c_char_p,C.c_size_t]
lib.tegu_air_render_pipeline_plan_draw_typed.restype=C.c_int32
lib.tegu_air_render_pipeline_native_check_fixed_ui_r85_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_char_p,C.c_size_t]
lib.tegu_air_render_pipeline_native_check_fixed_ui_r85_texture.restype=C.c_int32
lib.tegu_air_render_pipeline_plan_fixed_ui_r85_texture.argtypes=[C.c_void_p,C.POINTER(Res),C.c_size_t,C.POINTER(Draw),C.POINTER(FixedPlan),C.c_char_p,C.c_size_t]
lib.tegu_air_render_pipeline_plan_fixed_ui_r85_texture.restype=C.c_int32
lib.tegu_air_blob_free.argtypes=[C.POINTER(Blob)]
lib.tegu_air_render_pipeline_close.argtypes=[C.c_void_p];lib.tegu_air_library_close.argtypes=[C.c_void_p]

def data(path):
 b=Path(path).read_bytes(); return b,C.create_string_buffer(b)
metal,metalbuf=data(root/'research/tegu-gpu/donor-shaders/QuartzCore.metallib')
err=C.create_string_buffer(2048)
base=lib.tegu_air_library_open(metalbuf,len(metal),err,len(err));assert base,err.value
fragvals={
'fc_attachment_count':(49,b'\x01'),'fc_blend_function':(49,b'\x36'),'fc_clamp_color':(53,b'\x00'),'fc_color_inputs0':(49,b'\x00'),'fc_color_inputs1':(49,b'\x00'),'fc_color_opaque0':(53,b'\x00'),'fc_color_opaque1':(53,b'\x00'),'fc_color_opcodes0':(51,b'\0\0\0'),'fc_color_opcodes1':(51,b'\0\0\0'),'fc_coord_function0':(49,b'\x00'),'fc_coord_function1':(49,b'\x00'),'fc_debug_xr':(53,b'\x00'),'fc_dest_count':(49,b'\x00'),'fc_dest_index':(49,b'\x00'),'fc_destination_function':(49,b'\x00'),'fc_extended_range':(53,b'\x00'),'fc_filter0':(49,b'\x00'),'fc_filter1':(49,b'\x00'),'fc_framebuffer_fetch':(53,b'\x01'),'fc_framebuffer_sample':(53,b'\x00'),'fc_generic':(53,b'\x00'),'fc_has_asg_77_weights':(53,b'\x00'),'fc_has_attachment_0':(53,b'\x00'),'fc_has_attachment_1':(53,b'\x00'),'fc_has_attachment_2':(53,b'\x00'),'fc_has_color_cube0':(53,b'\x00'),'fc_has_color_cube1':(53,b'\x00'),'fc_has_color_params':(53,b'\x00'),'fc_has_color_trc0':(53,b'\x00'),'fc_has_color_trc1':(53,b'\x00'),'fc_has_edr_factor':(53,b'\x00'),'fc_has_gamma_luts':(53,b'\x00'),'fc_has_hdr_scale':(53,b'\x00'),'fc_has_image_0A':(53,b'\x01'),'fc_has_image_0B':(53,b'\x00'),'fc_has_image_0C':(53,b'\x00'),'fc_has_image_1A':(53,b'\x00'),'fc_has_image_1B':(53,b'\x00'),'fc_has_image_1C':(53,b'\x00'),'fc_has_lod_bias':(53,b'\x01'),'fc_has_lut_tex':(53,b'\x00'),'fc_has_noise_scale':(53,b'\x00'),'fc_has_noise_tex':(53,b'\x00'),'fc_has_premultiply':(53,b'\x00'),'fc_has_ycbcr_matrix':(53,b'\x00'),'fc_image_count':(49,b'\x01'),'fc_image_function0':(49,b'\x00'),'fc_image_function1':(49,b'\x00'),'fc_memoryless_index0':(49,b'\x00'),'fc_memoryless_index1':(49,b'\x00'),'fc_noise_width':(49,b'\x00'),'fc_plane_count0':(49,b'\x01'),'fc_plane_count1':(49,b'\x00'),'fc_texcoord_count0':(49,b'\x01'),'fc_texcoord_count1':(49,b'\x00'),'fc_texture_function':(49,b'\x01'),'fc_unbounded_range':(53,b'\x00'),'fc_vertex_layout':(49,b'\x00')}
vertvals={'fc_generic':(53,b'\x00'),'fc_texcoord_count0':(49,b'\x01'),'fc_texcoord_count1':(49,b'\x00'),'fc_vertex_layout':(49,b'\x00')}
keep=[]
def spec(entry,vals):
 arr=(FC*len(vals))()
 for i,(name,(ty,val)) in enumerate(vals.items()):
  nb=name.encode(); nbuf=C.create_string_buffer(nb);keep.append(nbuf)
  arr[i].kind=1;arr[i].type=ty;arr[i].size=len(val);arr[i].name=C.cast(nbuf,C.POINTER(C.c_ubyte));arr[i].name_len=len(nb)
  for j,x in enumerate(val):arr[i].data[j]=x
 eb=C.create_string_buffer(entry.encode());keep.append(eb);err.value=b''
 out=lib.tegu_air_library_specialize(base,eb,len(entry),arr,len(vals),err,len(err));print('spec',entry,bool(out),err.value.decode());assert out
 return out
vl=spec('fixed_vert_lph_spc',vertvals);fl=spec('fixed_frag_lph_cph',fragvals)
# parse profile header
h=(root/'research/compositor-ui-b3-r32-fixed-ui-index6/tegu-air-profile.h').read_text(); nums=[int(x) for x in re.search(r'\{(.*)\}',h,re.S).group(1).split(',') if x.strip()]; prof=bytes(nums);profbuf=C.create_string_buffer(prof)
attrs=(Attr*4)(Attr(0,31,0,1),Attr(1,29,16,1),Attr(2,29,24,1),Attr(3,27,32,1)); lays=(Layout*1)(Layout(1,48,1,1))
vs=Blob();fs=Blob();meta=Blob();vn=C.create_string_buffer(b'fixed_vert_lph_spc');fn=C.create_string_buffer(b'fixed_frag_lph_cph');err.value=b''
p=lib.tegu_air_render_pipeline_create_with_vertex_descriptor(vl,vn,18,fl,fn,18,80,attrs,4,lays,1,profbuf,len(prof),C.byref(vs),C.byref(fs),C.byref(meta),err,len(err));print('pipeline',bool(p),err.value.decode(),vs.length,fs.length,meta.length);assert p
err.value=b'';assert lib.tegu_air_render_pipeline_native_check_fixed_ui_r85_texture(p,70,err,len(err))!=0
err.value=b'';rc=lib.tegu_air_render_pipeline_native_check_fixed_ui_r85_texture(p,80,err,len(err));assert rc==0,err.value
resources=(Res*7)()
for i,(st,idx,off,leng) in enumerate([(1,2,19,512),(1,3,0,64),(2,1,277,512),(2,2,0,64),(1,1,0,288)]): resources[i].kind=1;resources[i].stage=st;resources[i].index=idx;resources[i].offset=off;resources[i].length=leng
resources[5].kind=2;resources[5].stage=2;resources[5].index=3;resources[5].format=70;resources[5].texture_type=2;resources[5].width=16;resources[5].height=16;resources[5].array_length=1;resources[5].mipmap_levels=1;resources[5].sample_count=1;resources[5].usage=1;resources[5].swizzle[:]=(2,3,4,5);resources[5].address_mode_identity=1;resources[5].resource_id=0x100003
resources[6].kind=3;resources[6].stage=2;resources[6].index=0;resources[6].normalized_coordinates=1;resources[6].max_anisotropy=1
draw=Draw(1,0,3,1,0);fixed=FixedPlan();err.value=b''
rc=lib.tegu_air_render_pipeline_plan_fixed_ui_r85_texture(p,resources,7,C.byref(draw),C.byref(fixed),err,len(err));assert rc==0,err.value
assert C.sizeof(FixedPlan)==336
assert (fixed.version,fixed.binding_count,fixed.push_count,fixed.push_bytes_length)==(1,4,2,20)
assert [(b.stage,b.index,b.descriptor_set,b.binding,b.descriptor_offset,b.descriptor_length) for b in fixed.bindings]==[(1,2,0,0,0,96),(1,3,0,1,0,16),(2,1,0,2,256,192),(2,2,0,3,0,16)]
assert (fixed.texture.texture_stage,fixed.texture.texture_index,fixed.texture.sampler_stage,fixed.texture.sampler_index,fixed.texture.descriptor_set,fixed.texture.binding,fixed.texture.resource_id)==(2,3,2,0,0,5,0x100003)
assert [(x.stage,x.offset,x.length,x.bytes_offset) for x in fixed.pushes]==[(1,0,8,0),(16,64,12,8)]
assert bytes(fixed.constant_bytes)==bytes([0,126,0,126,0,126,0,126])
assert (fixed.specialization_id,fixed.specialization_length,bytes(fixed.specialization_bytes))==(0,4,b'\0\0\0\0')
resources[5].resource_id=0;refused=FixedPlan();err.value=b''
assert lib.tegu_air_render_pipeline_plan_fixed_ui_r85_texture(p,resources,7,C.byref(draw),C.byref(refused),err,len(err))!=0
assert refused.version==0
result={'gate':'pass','plan':'pass','plan_size':C.sizeof(FixedPlan),'vertex_sha256':hashlib.sha256(C.string_at(vs.bytes,vs.length)).hexdigest(),'fragment_sha256':hashlib.sha256(C.string_at(fs.bytes,fs.length)).hexdigest(),'buffer_ranges':[[b.descriptor_offset,b.descriptor_length] for b in fixed.bindings]}
print(json.dumps(result,indent=2))
for b in [vs,fs,meta]:lib.tegu_air_blob_free(C.byref(b))
lib.tegu_air_render_pipeline_close(p);lib.tegu_air_library_close(vl);lib.tegu_air_library_close(fl);lib.tegu_air_library_close(base)
