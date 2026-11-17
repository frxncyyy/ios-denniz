//! C ABI for native Metal library/function/pipeline callers. No subprocesses,
//! fixture/name dispatch, Vulkan runtime, or GPU execution. Profile is a compiler
//! constraint supplied by the caller, NOT a claim of implemented Vulkan support.
use std::{ffi::c_char, panic::{catch_unwind, AssertUnwindSafe}, ptr, slice, str};
use metal2vulkan::{Budget, cache::VariantKey, device::{Device, DeviceDescription, DevicePolicy},
    error::{Error as TranslatorError, RuntimeInput},
    library::{Library as AirLibrary, FunctionStage, FunctionConstantValues, FunctionConstantValue}, pipeline::{ComputePipeline, ComputePipelineDescriptor, FunctionDescriptor, StageSource, Stage, SlotClass},
    bind::{BindState, BoundBuffer, StageTable, Dispatch, BindingPlan, DispatchCommands, WriteSource, UseTarget, AccessClass}, vk};
use metal2vulkan::metal::TextureSwizzleChannels;

/// Shared parsed library bytes plus per-function specialization snapshots. The
/// public C handle remains opaque; cloning retains the original AIR allocation.
#[derive(Clone)]
pub struct Library { raw:AirLibrary, constants:std::collections::BTreeMap<String,FunctionConstantValues> }
impl std::ops::Deref for Library {type Target=AirLibrary;fn deref(&self)->&AirLibrary {&self.raw}}
impl Library {
 fn parse(bytes:&[u8])->Result<Self,metal2vulkan::error::Error> {
  Ok(Self{raw:AirLibrary::parse(bytes)?,constants:Default::default()})
 }
 fn descriptor(&self,name:&str)->Result<FunctionDescriptor,String> {
  let mut d=FunctionDescriptor::new(self.raw.function(name).map_err(|e|e.to_string())?);
  d.constants=self.constants.get(name).cloned().unwrap_or_default();Ok(d)
 }
}
#[repr(C)]
pub struct FunctionConstant {pub kind:u32,pub ty:u32,pub index:u32,pub size:u32,
 pub name:*const u8,pub name_len:usize,pub data:[u8;64]}
/// Clone library state and snapshot this function's named/indexed constants.
/// Known declarations are validated now; complete lowering is at pipeline creation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_library_specialize(library:*const Library,
 name:*const u8,name_len:usize,constants:*const FunctionConstant,count:usize,
 error:*mut c_char,cap:usize)->*mut Library {
 let result=unsafe {attempt(error,cap,|| {
  if library.is_null()||count>256||(count>0&&constants.is_null()){return Err("invalid function constants".into());}
  let name=str::from_utf8(input(name,name_len,4096)?).map_err(|e|e.to_string())?;
  let original=&*library;let function=original.raw.function(name).map_err(|e|e.to_string())?;
  let mut values=FunctionConstantValues::defaults();
  for i in 0..count {
   let c=&*constants.add(i);
   if c.size==0||c.size>64{return Err("invalid constant byte count".into());}
   let ty=metal2vulkan::metal::DataType::try_from(c.ty).map_err(|e|e.to_string())?;
   let value=FunctionConstantValue::new(ty,c.data[..c.size as usize].to_vec());
   match c.kind {
    0=>{if c.name_len!=0||values.by_index.insert(c.index,value).is_some(){return Err("invalid or duplicate indexed constant".into());}},
    1=>{let key=str::from_utf8(input(c.name,c.name_len,4096)?).map_err(|e|e.to_string())?;
        if key.is_empty()||values.by_name.insert(key.to_owned(),value).is_some(){return Err("invalid or duplicate named constant".into());}},
    _=>return Err("invalid constant selector".into()),
   }
  }
  for constant in function.function_constants_dictionary().map_err(|e|e.to_string())?.values() {
   // The backend rejects supplying one declaration by both selectors.
   let named=values.by_name.get(constant.name());let indexed=values.by_index.get(&constant.index());
   if named.is_some()&&indexed.is_some(){return Err(format!("duplicate constant binding: {}",constant.name()));}
   let supplied=named.or(indexed);
   if let Some(v)=supplied {
    if v.ty!=constant.data_type(){return Err(format!("constant type mismatch: {}",constant.name()));}
   } else if constant.is_required(){return Err(format!("missing required constant: {}",constant.name()));}
  }
  let mut snapshot=original.clone();snapshot.constants.insert(name.to_owned(),values);Ok(snapshot)
 })};
 result.map(|l|Box::into_raw(Box::new(l))).unwrap_or(ptr::null_mut())
}

pub struct Pipeline {
    device: Device,
    pipeline: ComputePipeline,
    creation_only: bool,
    /* Dispatch-time bound-format/sampler variants: the base pipeline is a
     * creation compile (possibly with storage-format stand-ins), so a v2
     * dispatch that needs a translator variant compiles and caches it here.
     * Bounded so a command stream cannot grow the cache without limit. */
    function: FunctionDescriptor,
    variants: std::sync::Mutex<Vec<(VariantKey,ComputePipeline)>>,
    /* MTLComputePipelineDescriptor inputs beyond the function (tegu_air_pipeline_create_ex):
     * every compile of this pipeline (creation and dispatch variants) uses them. */
    extra: ComputeExtra,
}
const COMPUTE_VARIANTS:usize=8;
/// `stageInputDescriptor` and `maxCallStackDepth` of a compute pipeline descriptor
/// (shader-coverage-opus). Default = Metal's: no stage input, depth 1.
#[derive(Clone,Debug,Default)]
pub struct ComputeExtra {
    stage_input: Option<metal2vulkan::pipeline::StageInputDescriptor>,
    max_call_stack_depth: u32,
}
impl ComputeExtra {
    /// The kernel's lowering inputs under variant `key`, as the translator's own
    /// `kernel_inputs` derives them from a `Function` compute descriptor.
    fn inputs(&self,key:&VariantKey)->metal2vulkan::pipeline::LoweringInputs {
        let mut inputs=key.lowering_inputs(Stage::Kernel);
        if self.stage_input.is_some(){inputs.stage_input=self.stage_input.clone();}
        if self.max_call_stack_depth>1 {inputs.max_call_stack_depth=Some(self.max_call_stack_depth);}
        inputs
    }
    fn descriptor(&self,compiled:metal2vulkan::library::CompiledFunction)->ComputePipelineDescriptor {
        let mut d=ComputePipelineDescriptor::new(StageSource::Compiled(compiled));
        d.stage_input=self.stage_input.clone();
        if self.max_call_stack_depth>1 {d.max_call_stack_depth=self.max_call_stack_depth;}
        d
    }
}

/// The translator device of a profile text: derived once per distinct text in this process.
/// A `Device` is immutable (`Arc`, `Send + Sync`) and every creation, deserialization and
/// pack hit of a process passes the same profile, whose JSON parse and fingerprint
/// (~0.5 MB of JSON) were a large part of a small pipeline's AIR time.  Failures are not
/// cached and report exactly what the uncached derivation reports.
fn device_for_profile(json:&str)->Result<Device,String> {
    static CACHE:std::sync::Mutex<Vec<(Box<str>,Device)>>=std::sync::Mutex::new(Vec::new());
    if let Ok(cache)=CACHE.lock() {
        if let Some((_,device))=cache.iter().find(|(text,_)|&**text==json) {return Ok(device.clone());}
    }
    let description=DeviceDescription::from_khronos_json(json,None).map_err(|e|e.to_string())?;
    let device=Device::new(description,DevicePolicy::default()).map_err(|e|e.to_string())?;
    if let Ok(mut cache)=CACHE.lock() {
        if cache.len()<4&&!cache.iter().any(|(text,_)|&**text==json) {cache.push((json.into(),device.clone()));}
    }
    Ok(device)
}
/// Compile a kernel with the storage texture formats needed to create its pipeline state.
/// These formats answer creation-time compiler queries only. A resulting pipeline is never a
/// dispatch variant; dispatch requires a separate compile for the bound texture formats.
fn compile_compute_creation(
    device:&Device, function:&FunctionDescriptor, budget:&Budget,
) -> Result<((metal2vulkan::library::CompiledFunction,ComputePipeline),VariantKey,bool),TranslatorError> {
    compile_compute_creation_ex(device,function,budget,&ComputeExtra::default())
}
fn compile_compute_creation_ex(
    device:&Device, function:&FunctionDescriptor, budget:&Budget, extra:&ComputeExtra,
) -> Result<((metal2vulkan::library::CompiledFunction,ComputePipeline),VariantKey,bool),TranslatorError> {
    use metal2vulkan::{metal::{PixelFormat,format::storage_image_choice},pipeline::{LoweringInputs,SlotRef}};
    const STAND_INS:[PixelFormat;3]=[PixelFormat::R32Float,PixelFormat::R32Uint,PixelFormat::R32Sint];
    const MOST_SLOTS:usize=4;
    fn needed(error:&TranslatorError)->Option<SlotRef> {
        match error {
            TranslatorError::NeedsInput(RuntimeInput::StorageTextureFormat{slot})=>Some(*slot),
            _=>None,
        }
    }
    let compile=|key:&VariantKey| -> Result<_,TranslatorError> {
        let inputs:LoweringInputs=extra.inputs(key);
        let compiled=device.compile_function(function,&inputs,budget)?;
        let pipeline=device.compile_compute_pipeline(&extra.descriptor(compiled.clone()),budget)?;
        Ok((compiled,pipeline))
    };
    let empty=VariantKey::new();
    let first=match compile(&empty) {
        Ok(value)=>return Ok((value,empty,false)),
        Err(error)=>error,
    };
    let Some(first_slot)=needed(&first) else {return Err(first)};
    let choices:Vec<_>=STAND_INS.iter().filter_map(|&format|
        storage_image_choice(format,device.profile()).ok()).collect();
    if choices.is_empty(){return Err(first)}
    let mut slots=vec![(first_slot,0usize)];
    loop {
        let mut key=VariantKey::new();
        for &(slot,index) in &slots {key.storage_images.insert(slot,choices[index]);}
        match compile(&key) {
            Ok(value)=>return Ok((value,key,true)),
            Err(error)=>match needed(&error) {
                Some(slot) if !key.storage_images.contains_key(&slot)&&slots.len()<MOST_SLOTS=>slots.push((slot,0)),
                _=>loop {
                    let Some(last)=slots.last_mut() else {return Err(first)};
                    last.1+=1;
                    if last.1<choices.len(){break}
                    slots.pop();
                },
            }
        }
    }
}
pub struct RenderPipelineHandle {
    _device: Device,
    _pipeline: metal2vulkan::pipeline::RenderPipeline,
    source_blend: Option<RenderBlend>,
    requested_attachments: Vec<RenderColorAttachment>,
    requested_attachment_blends: Option<Vec<RenderBlend>>,
    /* Creation inputs the metadata blob depends on; kept so a cached
     * pipeline (tegu_air_render_pipeline_serialize) rebuilds identical outputs. */
    color_format: u32,
    explicit_targets: bool,
    /* Shape-level binding plans already made for this pipeline. RenderPipeline::plan
     * is "a pure function of the pipeline, device (the device that compiled it),
     * and the shape" (metal2vulkan bind/graphics.rs; bind/mod.rs: "memoize it on
     * (PipelineKey, BindShape)"), so a plan made for an equal shape is returned
     * as is. Shapes abstract buffer offsets to their alignment class, so draws
     * that differ only in ring-buffer offsets share a plan; per-draw bytes still
     * come from instantiate_draw. Refusals are not kept. */
    plan_cache: std::sync::Mutex<Vec<(metal2vulkan::bind::BindShape,std::sync::Arc<BindingPlan>)>>,
    /* Translator variants (BindingPlan::variant: sRGB raw views, cross-class
     * reads, XR/YCbCr in-shader forms, ...). The descriptor the base pipeline
     * was compiled from compiles them; a pipeline rebuilt from a cache gets it
     * back through tegu_air_render_pipeline_attach_source. Compiled variants
     * travel with the serialized pipeline (packs and disk caches). */
    descriptor: std::sync::Mutex<Option<metal2vulkan::pipeline::RenderPipelineDescriptor>>,
    variants: std::sync::Mutex<Vec<std::sync::Arc<VariantPipeline>>>,
}
/// One translator variant of a render pipeline: Device::compile_render_variant
/// of the base descriptor under `key`, with its own shape-level plan cache.
pub struct VariantPipeline {
    key: VariantKey,
    hash: u64,
    pipeline: metal2vulkan::pipeline::RenderPipeline,
    plan_cache: std::sync::Mutex<Vec<(metal2vulkan::bind::BindShape,std::sync::Arc<BindingPlan>)>>,
}
const RENDER_VARIANT_LIMIT:usize=16;
/// FNV-1a 64 of the variant key's serialized form: deterministic across
/// processes and builds (BTree-ordered serde), the frontend's variant id.
fn variant_key_hash(key:&VariantKey)->u64 {
    let bytes=serde_json::to_vec(key).unwrap_or_default();
    let mut h:u64=0xcbf29ce484222325;
    for b in bytes {h^=u64::from(b);h=h.wrapping_mul(0x100000001b3);}
    h
}
impl VariantPipeline {
    fn plan(&self,device:&Device,state:&BindState)->Result<std::sync::Arc<BindingPlan>,String>{
        let hit={
            let cache=self.plan_cache.lock().unwrap_or_else(|e|e.into_inner());
            state.position_of_shape(cache.iter().map(|(shape,_)|shape)).map(|i|cache[i].1.clone())
        };
        if let Some(plan)=hit{return Ok(plan);}
        let shape=state.shape();
        let plan=std::sync::Arc::new(self.pipeline.plan(device,&shape).map_err(|e|e.to_string())?);
        let mut cache=self.plan_cache.lock().unwrap_or_else(|e|e.into_inner());
        if !cache.iter().any(|(s,_)|*s==shape){
            if cache.len()>=RENDER_PLAN_CACHE_LIMIT{cache.remove(0);}
            cache.push((shape,plan.clone()));
        }
        Ok(plan)
    }
}
impl RenderPipelineHandle {
    /// The compiled variant `key`, compiling it from the base descriptor on
    /// first use. Without a descriptor (a cached pipeline whose source was
    /// not attached) a missing variant is an explicit refusal.
    fn variant_for(&self,key:&VariantKey)->Result<std::sync::Arc<VariantPipeline>,String>{
        if let Some(v)=self.variants.lock().unwrap_or_else(|e|e.into_inner()).iter().find(|v|v.key==*key){return Ok(v.clone());}
        /* An AIR-planned pipeline's code is the AIR compiler's: no translator variant
         * exists for it (the AIR adapter states every runtime fact it handles). */
        if self.air_planned(){return Err("AIR-planned pipeline: the draw needs a metal2vulkan variant (sRGB/YCbCr/cubic/cross-class/range facts), which the AIR code does not state".into());}
        let descriptor=self.descriptor.lock().unwrap_or_else(|e|e.into_inner()).clone()
            .ok_or("translator variant needs the pipeline source (cached pipeline without attached source)")?;
        let pipeline=self._device.compile_render_variant(&descriptor,key,&Budget::standard()).map_err(|e|e.to_string())?;
        let v=std::sync::Arc::new(VariantPipeline{key:key.clone(),hash:variant_key_hash(key),pipeline,plan_cache:Default::default()});
        let mut variants=self.variants.lock().unwrap_or_else(|e|e.into_inner());
        if let Some(existing)=variants.iter().find(|e|e.key==*key){return Ok(existing.clone());}
        if variants.len()>=RENDER_VARIANT_LIMIT{return Err("too many translator variants for one pipeline".into());}
        variants.push(v.clone());
        Ok(v)
    }
    /// Planned from AIR metadata (tegu_air_render_pipeline_create_planned): its stages
    /// are StageModule::external (a SPIR-V header only), never a translation.
    fn air_planned(&self)->bool{
        !self._pipeline.stages.is_empty()&&self._pipeline.stages.iter().all(|s|s.spirv.get(2)==Some(&AIR_PLAN_GENERATOR))
    }
    fn variant_by_hash(&self,hash:u64)->Option<std::sync::Arc<VariantPipeline>>{
        self.variants.lock().unwrap_or_else(|e|e.into_inner()).iter().find(|v|v.hash==hash).cloned()
    }
}
const RENDER_PLAN_CACHE_LIMIT:usize=8;
/* TEGU_AIR_PLAN_CACHE=0 bypasses the cache; TEGU_AIR_PLAN_CACHE_VERIFY=1 also
 * re-plans behind every hit and panics on any difference (host evidence). */
fn render_plan_cache_mode()->(bool,bool){
    static MODE:std::sync::OnceLock<(bool,bool)>=std::sync::OnceLock::new();
    *MODE.get_or_init(||(std::env::var("TEGU_AIR_PLAN_CACHE").map_or(true,|v|v!="0"),
        std::env::var("TEGU_AIR_PLAN_CACHE_VERIFY").is_ok_and(|v|v=="1")))
}
static RENDER_PLAN_CACHE_HITS:std::sync::atomic::AtomicU64=std::sync::atomic::AtomicU64::new(0);
static RENDER_PLAN_CACHE_MISSES:std::sync::atomic::AtomicU64=std::sync::atomic::AtomicU64::new(0);
impl RenderPipelineHandle {
    fn cached_plan(&self,state:&BindState)->Result<std::sync::Arc<BindingPlan>,String>{
        let (enabled,verify)=render_plan_cache_mode();
        self.cached_plan_mode(state,enabled,verify)
    }
    fn cached_plan_mode(&self,state:&BindState,enabled:bool,verify:bool)->Result<std::sync::Arc<BindingPlan>,String>{
        use std::sync::atomic::Ordering::Relaxed;
        if !enabled{return Ok(std::sync::Arc::new(self._pipeline.plan(&self._device,&state.shape()).map_err(|e|e.to_string())?));}
        let hit={
            let cache=self.plan_cache.lock().unwrap_or_else(|e|e.into_inner());
            state.position_of_shape(cache.iter().map(|(shape,_)|shape)).map(|i|cache[i].1.clone())
        };
        if let Some(plan)=hit{
            RENDER_PLAN_CACHE_HITS.fetch_add(1,Relaxed);
            if verify{
                let fresh=self._pipeline.plan(&self._device,&state.shape()).map_err(|e|e.to_string())?;
                assert!(fresh==*plan,"TEGU_AIR_PLAN_CACHE_VERIFY: cached plan differs from a fresh plan of the same shape");
            }
            return Ok(plan);
        }
        RENDER_PLAN_CACHE_MISSES.fetch_add(1,Relaxed);
        let shape=state.shape();
        let plan=std::sync::Arc::new(self._pipeline.plan(&self._device,&shape).map_err(|e|e.to_string())?);
        let mut cache=self.plan_cache.lock().unwrap_or_else(|e|e.into_inner());
        if !cache.iter().any(|(s,_)|*s==shape){
            if cache.len()>=RENDER_PLAN_CACHE_LIMIT{cache.remove(0);}
            cache.push((shape,plan.clone()));
        }
        Ok(plan)
    }
}
#[repr(C)]
pub struct VertexAttribute {pub index:u32,pub format:u32,pub offset:u32,pub buffer_index:u32}
#[repr(C)]
pub struct VertexLayout {pub index:u32,pub stride:u32,pub step_function:u32,pub step_rate:u32}
#[repr(C)]
#[derive(Clone,Copy,PartialEq,Eq)]
pub struct RenderColorAttachment {pub format:u32,pub write_mask:u32}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct RenderResource {
 pub kind:u32,pub stage:u32,pub index:u32,pub format:u32,
 pub texture_type:u32,pub width:u32,pub height:u32,pub array_length:u32,pub mipmap_levels:u32,pub sample_count:u32,pub usage:u32,
 pub sparse:u32,pub depth_twin:u32,pub swizzle:[u32;4],pub parent_level:u32,pub parent_width:u32,pub parent_height:u32,pub parent_depth:u32,pub own_format_views:u32,pub address_mode_identity:u32,
 pub length:u64,pub offset:u64,pub gpu_address:u64,pub resource_id:u64,pub stride:u32,pub reserved:u32,
 pub min_filter:u32,pub mag_filter:u32,pub mip_filter:u32,pub address_s:u32,pub address_t:u32,pub address_r:u32,pub border_color:u32,
 pub normalized_coordinates:u32,pub lod_average:u32,pub max_anisotropy:u32,pub compare_function:u32,pub reduction_mode:u32,
 pub lod_min_clamp:f32,pub lod_max_clamp:f32,
}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct RenderDraw {pub version:u32,pub vertex_start:u32,pub vertex_count:u32,pub instance_count:u32,pub base_instance:u32}
#[repr(C)]
#[derive(Clone,Copy,Default)]
pub struct RenderVertexAttribute {pub location:u32,pub buffer_index:u32,pub format:u32,
 pub offset:u32,pub stride:u32,pub input_rate:u32,pub divisor:u32}
#[repr(C)]
pub struct RenderVertexFetch {pub version:u32,pub mask:u32,pub count:u32,pub reserved:u32,
 pub attributes:[RenderVertexAttribute;31]}

fn render_vertex_fetch_input(input:&metal2vulkan::pipeline::VertexInputState)->Result<RenderVertexFetch,String> {
  use metal2vulkan::pipeline::VertexStride;
  if !input.pulled.is_empty()||!input.variant_pulled.is_empty()||input.attributes.len()>31||input.bindings.len()>31 {
   return Err("vertex fetch requires unsupported pulled inputs".into());
  }
  let mut fetch=RenderVertexFetch{version:1,mask:0,count:0,reserved:0,attributes:[RenderVertexAttribute::default();31]};
  for a in &input.attributes {
   if a.location>=31||fetch.mask&(1u32<<a.location)!=0{return Err("invalid linked vertex location".into());}
   let mut bindings=input.bindings.iter().filter(|b|b.binding==a.binding);
   let b=bindings.next().ok_or("missing vertex buffer binding")?;
   if bindings.next().is_some()||b.buffer_index>=31{return Err("invalid linked vertex binding".into());}
   let stride=match b.stride {VertexStride::Static(s)=>s,_=>return Err("dynamic vertex stride unsupported".into())};
   let format=a.format.as_raw() as u32;let rate=b.input_rate.as_raw() as u32;
   /* 83 is VK_FORMAT_R16G16_SFLOAT, the Metal Half2 attribute lowering
    * encodes as MALI_RG16F|RGB1.  The other six are the already-qualified
    * float formats. */
   if ![83,97,100,103,106,109].contains(&format)||rate>1||b.divisor>1||rate==0&&b.divisor!=1 {
    return Err("native vertex fetch format or divisor unsupported".into());
   }
   fetch.attributes[fetch.count as usize]=RenderVertexAttribute{location:a.location,buffer_index:b.buffer_index,
    format,offset:a.offset,stride,input_rate:rate,divisor:b.divisor};
   fetch.mask|=1u32<<a.location;fetch.count+=1;
  }
  Ok(fetch)
}

fn render_vertex_fetch(pipeline:&metal2vulkan::pipeline::RenderPipeline)->Result<RenderVertexFetch,String> {
  render_vertex_fetch_input(&pipeline.vertex_input)
}

/// Fixed-function fetch plan only, not permission to submit the pipeline. All
/// other shader/graphics requirements remain under the native execution gate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_vertex_fetch(pipeline:*const RenderPipelineHandle,
 out:*mut RenderVertexFetch,error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe {error_copy(error,cap,"null vertex fetch output")};return -1;}
 unsafe {ptr::write_bytes(out,0,1);}
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  render_vertex_fetch(&(*pipeline)._pipeline)
 })} {Some(fetch)=>{unsafe {*out=fetch};0},None=>-1}
}

/// Exact translator immutable (constexpr) sampler of descriptor set 0 / `binding`:
/// the VkSampler description the translated shader was compiled against, so a
/// consumer packs exactly what the shader expects (no Metal re-derivation).
/// Enums are the Vulkan raw values; floats are the desc's own values.
#[repr(C)]
#[derive(Clone,Copy,Debug,Default,PartialEq)]
pub struct ImmutableSampler {
 pub version:u32,pub binding:u32,pub min_filter:u32,pub mag_filter:u32,pub mipmap_mode:u32,
 pub address:[u32;3],pub border_color:u32,pub compare_enabled:u32,pub compare_op:u32,
 pub anisotropy_enabled:u32,pub reduction:u32,pub border_components_set:u32,
 pub mip_lod_bias:f32,pub max_anisotropy:f32,pub min_lod:f32,pub max_lod:f32,
}
const _:()=assert!(size_of::<ImmutableSampler>()==72);
fn immutable_sampler_of(pipeline:&metal2vulkan::pipeline::RenderPipeline,binding:u32)->Result<ImmutableSampler,String>{
 let desc=pipeline.layout.sets.iter().filter(|s|s.set==0).flat_map(|s|s.bindings.iter())
  .find(|b|b.binding==binding).ok_or("no set0 descriptor binding")?
  .immutable_sampler.as_ref().ok_or("descriptor binding has no immutable sampler")?;
 Ok(ImmutableSampler{version:1,binding,min_filter:desc.min_filter.as_raw() as u32,mag_filter:desc.mag_filter.as_raw() as u32,
  mipmap_mode:desc.mipmap_mode.as_raw() as u32,
  address:[desc.address_mode_u.as_raw() as u32,desc.address_mode_v.as_raw() as u32,desc.address_mode_w.as_raw() as u32],
  border_color:desc.border_color.as_raw() as u32,compare_enabled:desc.compare.is_some() as u32,
  compare_op:desc.compare.map_or(0,|c|c.as_raw() as u32),anisotropy_enabled:desc.max_anisotropy.is_some() as u32,
  reduction:desc.reduction.as_raw() as u32,border_components_set:desc.border_components.is_some() as u32,
  mip_lod_bias:desc.mip_lod_bias,max_anisotropy:desc.max_anisotropy.unwrap_or(1.0),min_lod:desc.min_lod,max_lod:desc.max_lod})
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_immutable_sampler(pipeline:*const RenderPipelineHandle,
 binding:u32,out:*mut ImmutableSampler,error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe {error_copy(error,cap,"null immutable sampler output")};return -1;}
 unsafe {ptr::write_bytes(out,0,1);}
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  immutable_sampler_of(&(*pipeline)._pipeline,binding)
 })} {Some(v)=>{unsafe {*out=v};0},None=>-1}
}

/// SPIR-V and metadata of the compiled translator variant whose key hash is
/// hi:lo (GraphicsSpecializationPlan.reserved[1..2] of a variant plan).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_variant_spirv(pipeline:*const RenderPipelineHandle,
 hash_lo:u32,hash_hi:u32,vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,
 error:*mut c_char,cap:usize)->i32 {
 if vertex_spirv.is_null()||fragment_spirv.is_null()||metadata.is_null()||
    vertex_spirv==fragment_spirv||vertex_spirv==metadata||fragment_spirv==metadata {
  unsafe {error_copy(error,cap,"invalid or aliased variant outputs")};return -1;
 }
 unsafe {*vertex_spirv=Blob{bytes:ptr::null_mut(),length:0};*fragment_spirv=Blob{bytes:ptr::null_mut(),length:0};
  *metadata=Blob{bytes:ptr::null_mut(),length:0};}
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  let handle=&*pipeline;
  let v=handle.variant_by_hash(u64::from(hash_lo)|(u64::from(hash_hi)<<32)).ok_or("unknown translator variant")?;
  render_outputs_for(handle,&v.pipeline)
 })} {Some((v,f,m))=>{unsafe {*vertex_spirv=blob(v);*fragment_spirv=blob(f);*metadata=blob(m)};0},None=>-1}
}
/// The bytes a GENERIC_COLOR_TABLE binding's buffer holds (process-lifetime,
/// identical for every pipeline and device).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_color_write_table(length:*mut usize)->*const u8 {
    let table=metal2vulkan::bind::color_write_table();
    if !length.is_null(){unsafe {*length=table.len();}}
    table.as_ptr()
}

/// Number of compiled translator variants (a change means the serialized
/// pipeline should be stored again so caches and packs carry them).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_variant_count(pipeline:*const RenderPipelineHandle)->u32 {
 if pipeline.is_null(){return 0;}
 unsafe {(*pipeline).variants.lock().map(|v|v.len() as u32).unwrap_or(0)}
}
/// Give a pipeline rebuilt from a cache the descriptor of its creation inputs
/// (the same arguments as tegu_air_render_pipeline_create*), so translator
/// variants it has not cached can be compiled. Builds the descriptor only; it
/// must describe the cached pipeline (the caller derived the cache key from
/// exactly these inputs).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_attach_source(pipeline:*const RenderPipelineHandle,
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 color_format:u32,attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,blend:*const RenderBlend,
 targets:*const RenderColorAttachment,target_count:usize,attachment_blends:*const RenderBlend,
 error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  let handle=&*pipeline;
  if handle.air_planned(){return Ok(());} /* no translator variants to compile from a source */
  if target_count>0&&!attachment_blends.is_null()&&!blend.is_null(){return Err("blend and attachment blends are exclusive".into());}
  let (descriptor,_)=render_descriptor_from_inputs(vertex_library,vertex_name,vertex_name_len,
   fragment_library,fragment_name,fragment_name_len,color_format,attributes,attribute_count,layouts,layout_count,
   blend,false,targets,target_count,attachment_blends)?;
  *handle.descriptor.lock().unwrap_or_else(|e|e.into_inner())=Some(descriptor);
  Ok(())
 })} {Some(())=>0,None=>-1}
}

/// Initial render descriptor surface: vertex/fragment functions, one unblended
/// RGBA8/BGRA8 target, one sample, no depth/stencil. The basic entry supplies no
/// vertex descriptor; the extended entry below accepts static vertex layouts. This
/// compiles both stages together; native draw binding/execution remains separate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_create(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 color_format:u32,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,
 error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 unsafe {tegu_air_render_pipeline_create_with_vertex_descriptor(
  vertex_library,vertex_name,vertex_name_len,fragment_library,fragment_name,fragment_name_len,
  color_format,ptr::null(),0,ptr::null(),0,profile,profile_len,
  vertex_spirv,fragment_spirv,metadata,error,cap)}
}
/// Copies the complete static vertex fetch descriptor into the actual linked
/// compiler input. This does not relax the native backend's resource checks.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_create_with_vertex_descriptor(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 color_format:u32,attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,
 error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 unsafe {render_pipeline_create(vertex_library,vertex_name,vertex_name_len,
  fragment_library,fragment_name,fragment_name_len,color_format,attributes,attribute_count,
  layouts,layout_count,ptr::null(),false,profile,profile_len,
  ptr::null(),0,ptr::null(),vertex_spirv,fragment_spirv,metadata,error,cap)}
}
/// Additive descriptor path: compile the requested blend state into the real
/// translator pipeline. Old creation/check entry points remain unblended.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_create_with_blend(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 color_format:u32,attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,blend:*const RenderBlend,
 profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,
 error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 unsafe {render_pipeline_create(vertex_library,vertex_name,vertex_name_len,
  fragment_library,fragment_name,fragment_name_len,color_format,attributes,attribute_count,
  layouts,layout_count,blend,true,profile,profile_len,
  ptr::null(),0,ptr::null(),vertex_spirv,fragment_spirv,metadata,error,cap)}
}
/// Explicit attachment path for bounded metadata planning. Every supplied
/// attachment and write mask is forwarded to the translator unchanged.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_create_with_attachments(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 attachments:*const RenderColorAttachment,attachment_count:usize,
 attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 unsafe {render_pipeline_create_attachments_checked(vertex_library,vertex_name,vertex_name_len,
  fragment_library,fragment_name,fragment_name_len,attachments,attachment_count,ptr::null(),false,
  attributes,attribute_count,layouts,layout_count,profile,profile_len,vertex_spirv,fragment_spirv,metadata,error,cap)}
}
/// Post-tessellation render pipeline for tessellation emulation (shader-coverage-opus): the same
/// inputs as tegu_air_render_pipeline_create_with_attachments; succeeds only for a pipeline the
/// translator expands to vertex/control/evaluation/fragment stages and returns the EVALUATION
/// stage SPIR-V as vertex_spirv (TessCoord, patch inputs: the caller's tessellator provides them)
/// with the fragment SPIR-V; metadata "tessellation_emulation" names the stage. Not a native draw.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_create_tessellated(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 attachments:*const RenderColorAttachment,attachment_count:usize,
 attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 TESSELLATED_OUTPUTS.with(|t|t.set(true));
 let h=unsafe {render_pipeline_create_attachments_checked(vertex_library,vertex_name,vertex_name_len,
  fragment_library,fragment_name,fragment_name_len,attachments,attachment_count,ptr::null(),false,
  attributes,attribute_count,layouts,layout_count,profile,profile_len,vertex_spirv,fragment_spirv,metadata,error,cap)};
 TESSELLATED_OUTPUTS.with(|t|t.set(false));
 h
}
/// Additive per-attachment blend path. The blend array has attachment_count
/// records; each record's write mask must match its corresponding attachment.
/// This compiles all declared targets without authorizing native execution.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_create_with_attachment_blends(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 attachments:*const RenderColorAttachment,attachment_count:usize,blends:*const RenderBlend,
 attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 unsafe {render_pipeline_create_attachments_checked(vertex_library,vertex_name,vertex_name_len,
  fragment_library,fragment_name,fragment_name_len,attachments,attachment_count,blends,true,
  attributes,attribute_count,layouts,layout_count,profile,profile_len,vertex_spirv,fragment_spirv,metadata,error,cap)}
}
unsafe fn render_pipeline_create_attachments_checked(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 attachments:*const RenderColorAttachment,attachment_count:usize,blends:*const RenderBlend,require_blends:bool,
 attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 let outputs=[(vertex_spirv as usize,size_of::<Blob>()),(fragment_spirv as usize,size_of::<Blob>()),(metadata as usize,size_of::<Blob>())];
 let inputs=[(vertex_library as usize,if vertex_library.is_null(){0}else{size_of::<Library>()}),
  (fragment_library as usize,if fragment_library.is_null(){0}else{size_of::<Library>()}),
  (vertex_name as usize,vertex_name_len),(fragment_name as usize,fragment_name_len),
  (attachments as usize,attachment_count.saturating_mul(size_of::<RenderColorAttachment>())),
  (attributes as usize,attribute_count.saturating_mul(size_of::<VertexAttribute>())),
  (layouts as usize,layout_count.saturating_mul(size_of::<VertexLayout>())),(profile as usize,profile_len),
  (blends as usize,if blends.is_null(){0}else{attachment_count.saturating_mul(size_of::<RenderBlend>())})];
 let error_span=(error as usize,cap);
 let bad_outputs=outputs.iter().any(|(p,n)|*p==0||inputs.iter().any(|(q,m)|compositor_ranges_overlap(*p,*n,*q,*m))||
  compositor_ranges_overlap(*p,*n,error_span.0,error_span.1))||
  (0..outputs.len()).any(|i|(i+1..outputs.len()).any(|j|compositor_ranges_overlap(outputs[i].0,outputs[i].1,outputs[j].0,outputs[j].1)));
 let error_aliases_input=error_span.0!=0&&inputs.iter().any(|(p,n)|compositor_ranges_overlap(error_span.0,error_span.1,*p,*n));
 if bad_outputs||error_aliases_input {
  if !error_aliases_input&&!outputs.iter().any(|(p,n)|compositor_ranges_overlap(error_span.0,error_span.1,*p,*n)){
   unsafe{error_copy(error,cap,"aliased compositor creation spans")};
  }
  return ptr::null_mut();
 }
 if require_blends&&(blends.is_null()||attachment_count==0||attachment_count>8) {
  unsafe{error_copy(error,cap,"explicit attachment blends require 1..8 matching records")};
  return ptr::null_mut();
 }
 unsafe {render_pipeline_create(vertex_library,vertex_name,vertex_name_len,fragment_library,fragment_name,fragment_name_len,
 0,attributes,attribute_count,layouts,layout_count,ptr::null(),false,profile,profile_len,attachments,attachment_count,
 blends,vertex_spirv,fragment_spirv,metadata,error,cap)}
}
/// The RenderPipelineDescriptor of a render creation's inputs (shared by
/// creation and tegu_air_render_pipeline_attach_source, so a cached pipeline
/// rebuilds exactly the descriptor its variants are compiled from).
unsafe fn render_descriptor_from_inputs(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 color_format:u32,attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,blend:*const RenderBlend,require_blend:bool,
 targets:*const RenderColorAttachment,target_count:usize,attachment_blends:*const RenderBlend)
 ->Result<(metal2vulkan::pipeline::RenderPipelineDescriptor,Vec<RenderColorAttachment>),String> { unsafe {
        use metal2vulkan::{metal::{PixelFormat,VertexFormat,VertexStepFunction},pipeline::{RenderPipelineDescriptor,ColorAttachmentDescriptor,
            VertexDescriptor,VertexAttributeDescriptor,VertexBufferLayoutDescriptor}};
        if vertex_library.is_null()||fragment_library.is_null() {return Err("null render library".into());}
        if attribute_count>31||layout_count>31||(attribute_count>0&&attributes.is_null())||
           (layout_count>0&&layouts.is_null())||target_count>8||(target_count>0&&targets.is_null()) {return Err("invalid render descriptor spans".into());}
        let mut fetch=VertexDescriptor::new();
        for i in 0..attribute_count {
            let a=&*attributes.add(i);
            if a.index>=31||a.buffer_index>=31 {return Err("vertex attribute index out of range".into());}
            let format=VertexFormat::try_from(a.format).map_err(|e|e.to_string())?;
            if format==VertexFormat::Invalid {return Err("unset vertex attribute supplied".into());}
            if fetch.attributes.insert(a.index,VertexAttributeDescriptor::new(format,a.offset,a.buffer_index)).is_some() {
                return Err("duplicate vertex attribute".into());
            }
        }
        for i in 0..layout_count {
            let l=&*layouts.add(i);
            if l.index>=31 {return Err("vertex layout index out of range".into());}
            let mut layout=VertexBufferLayoutDescriptor::new(Some(l.stride));
            layout.step_function=VertexStepFunction::try_from(l.step_function).map_err(|e|e.to_string())?;
            layout.step_rate=l.step_rate;
            if fetch.layouts.insert(l.index,layout).is_some() {return Err("duplicate vertex layout".into());}
        }
        // Blendable n2r colour targets (Mesa v9 pan_blendable_formats):
        // R8, R16F, B5G6R5, ABGR4, BGR5A1, RG16F, RGBA8(+sRGB), BGRA8(+sRGB),
        // RGB10A2, RG11B10F, BGR10A2, RGBA16F.
        let format=if target_count>0 {PixelFormat::try_from((*targets).format).map_err(|e|e.to_string())?} else {
            if !n2r_target_format(color_format){
                return Err("render bridge colour format is not an n2r target (blendable or raw)".into());}
            PixelFormat::try_from(color_format).map_err(|e|e.to_string())?
        };
        let vertex=str::from_utf8(input(vertex_name,vertex_name_len,4096)?).map_err(|e|e.to_string())?;
        let fragment=str::from_utf8(input(fragment_name,fragment_name_len,4096)?).map_err(|e|e.to_string())?;
        let vertex=(&*vertex_library).descriptor(vertex)?;
        let fragment=(&*fragment_library).descriptor(fragment)?;
        let mut descriptor=RenderPipelineDescriptor::new(StageSource::Function(vertex));
        descriptor.fragment=Some(StageSource::Function(fragment));
        let mut attachment_snapshot=Vec::new();
        if target_count==0 {
            let mut color=ColorAttachmentDescriptor::new(format);
            if require_blend&&blend.is_null(){return Err("null render blend descriptor".into());}
            if !blend.is_null(){(*blend).apply(&mut color)?;}
            descriptor.color_attachments[0]=Some(color);
            attachment_snapshot.push(RenderColorAttachment{format:color_format,write_mask:if blend.is_null(){15}else{(*blend).write_mask}});
        } else {
            if !blend.is_null()||require_blend{return Err("explicit attachment prototype does not combine with blend descriptor".into());}
            for i in 0..target_count {
                let spec=*targets.add(i);
                let pf=PixelFormat::try_from(spec.format).map_err(|e|e.to_string())?;
                if !n2r_target_format(spec.format)||spec.write_mask>15 {
                    return Err("attachment format or write mask outside bounded class".into());
                }
                let mut color=ColorAttachmentDescriptor::new(pf);
                color.write_mask=metal2vulkan::metal::ColorWriteMask::try_from(spec.write_mask).map_err(|e|e.to_string())?;
                if !attachment_blends.is_null() {
                    let requested=&*attachment_blends.add(i);
                    if requested.write_mask!=spec.write_mask{return Err("attachment blend/write mask mismatch".into());}
                    requested.apply(&mut color)?;
                }
                descriptor.color_attachments[i]=Some(color);
                attachment_snapshot.push(spec);
            }
        }
        if attribute_count>0||layout_count>0 {descriptor.vertex_descriptor=Some(fetch);}
        Ok((descriptor,attachment_snapshot))
}}
unsafe fn render_pipeline_create(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 color_format:u32,attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,blend:*const RenderBlend,require_blend:bool,
 profile:*const u8,profile_len:usize,
 targets:*const RenderColorAttachment,target_count:usize,attachment_blends:*const RenderBlend,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,
 error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
    if vertex_spirv.is_null()||fragment_spirv.is_null()||metadata.is_null()||
       vertex_spirv==fragment_spirv||vertex_spirv==metadata||fragment_spirv==metadata {
        unsafe {error_copy(error,cap,"invalid or aliased render outputs")};
        return ptr::null_mut();
    }
    unsafe {
        *vertex_spirv=Blob{bytes:ptr::null_mut(),length:0};
        *fragment_spirv=Blob{bytes:ptr::null_mut(),length:0};
        *metadata=Blob{bytes:ptr::null_mut(),length:0};
    }
    let result=unsafe {attempt(error,cap,|| {
        let (descriptor,attachment_snapshot)=render_descriptor_from_inputs(vertex_library,vertex_name,vertex_name_len,
         fragment_library,fragment_name,fragment_name_len,color_format,attributes,attribute_count,layouts,layout_count,
         blend,require_blend,targets,target_count,attachment_blends)?;
        let json=str::from_utf8(input(profile,profile_len,1024*1024)?).map_err(|e|e.to_string())?;
        let device=device_for_profile(json)?;
        let pipeline=device.compile_render_pipeline(&descriptor,&Budget::standard()).map_err(|e|e.to_string())?;
        let handle=RenderPipelineHandle{_device:device,_pipeline:pipeline,source_blend:if blend.is_null(){None}else{Some(*blend)},requested_attachments:attachment_snapshot,requested_attachment_blends:if attachment_blends.is_null(){None}else{Some((0..target_count).map(|i|*attachment_blends.add(i)).collect())},color_format,explicit_targets:target_count>0,plan_cache:Default::default(),
            descriptor:std::sync::Mutex::new(Some(descriptor)),variants:Default::default()};
        let (vertex,fragment,encoded)=render_outputs(&handle)?;
        Ok((vertex,fragment,encoded,handle))
    })};
    match result {
        Some((vertex,fragment,meta,handle))=>{
            unsafe {*vertex_spirv=blob(vertex);*fragment_spirv=blob(fragment);*metadata=blob(meta)};
            Box::into_raw(Box::new(handle))
        },None=>ptr::null_mut(),
    }
}
/// SPIR-V and metadata blobs of a created render pipeline. One derivation for
/// fresh compiles and cached pipelines, so both give identical bytes.
fn render_outputs(handle:&RenderPipelineHandle)->Result<(Vec<u8>,Vec<u8>,Vec<u8>),String> {
 render_outputs_for(handle,&handle._pipeline)
}
thread_local! {
    /* tegu_air_render_pipeline_create_tessellated only: a post-tessellation pipeline's
     * evaluation stage is returned as the "vertex" SPIR-V (shader-coverage-opus). */
    static TESSELLATED_OUTPUTS:std::cell::Cell<bool>=const {std::cell::Cell::new(false)};
}
fn render_outputs_for(handle:&RenderPipelineHandle,pipeline:&metal2vulkan::pipeline::RenderPipeline)->Result<(Vec<u8>,Vec<u8>,Vec<u8>),String> {
        let tess=TESSELLATED_OUTPUTS.with(|t|t.get())&&pipeline.stages.len()==4&&
            pipeline.stages.iter().any(|s|s.stage==vk::ShaderStage::TESSELLATION_EVALUATION);
        if pipeline.stages.len()!=2&&!tess {return Err("unsupported expanded render stage topology".into());}
        let first=if tess {vk::ShaderStage::TESSELLATION_EVALUATION} else {vk::ShaderStage::VERTEX};
        let vertex=pipeline.stages.iter().find(|s|s.stage==first).ok_or("missing vertex stage")?;
        let fragment=pipeline.stages.iter().find(|s|s.stage==vk::ShaderStage::FRAGMENT).ok_or("missing fragment stage")?;
        let validation=handle._device.profile().spirv_validation();
        let stages:Vec<_>=pipeline.stages.iter().map(|s|serde_json::json!({
            "stage":s.stage,"entry_point":s.entry_point,"specialization":s.specialization,
            "required_subgroup_size":s.required_subgroup_size,"stage_flags":s.stage_flags,
            "constant_blob":s.constant_blob.as_ref().map(|b|b.to_vec()),
            "rounded_colors":s.rounded_colors,"half_bit_colors":s.half_bit_colors,
            "precision_notes":s.precision_notes,"reconvergence":s.reconvergence
        })).collect();
        let mut meta=serde_json::json!({"schema":1,"scope":"render_compilation_not_native_draw_plan",
            "color_format":handle.color_format,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},
            "profile":pipeline.profile,"stages":stages,"requirements":{
                "api_version":pipeline.requirements.api_version,
                "spirv_capabilities":pipeline.requirements.spirv_capabilities,
                "spirv_extensions":pipeline.requirements.spirv_extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
                "features":pipeline.requirements.features,"extensions":pipeline.requirements.extensions,
                "properties":pipeline.requirements.properties.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),"limits":pipeline.requirements.limits.iter().map(|(k,v)|(format!("{k:?}"),*v)).collect::<std::collections::BTreeMap<_,_>>(),
                "formats":pipeline.requirements.formats.iter().map(|(f,u)|(f,format!("{u:?}"))).collect::<Vec<_>>(),"stage_flags":pipeline.requirements.stage_flags.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
                "denorm_independence":pipeline.requirements.denorm_independence,
                "rounding_independence":pipeline.requirements.rounding_independence},
            "layout":pipeline.layout,"interface":pipeline.interface,"dependencies":pipeline.dependencies,
            "vertex_input":pipeline.vertex_input,"graphics":pipeline.graphics,"variant_bits":pipeline.variant_bits});
        // Preserve every legacy metadata byte. Only the additive explicit
        // attachment entry reports the original caller descriptor separately.
        if handle.explicit_targets {
            meta.as_object_mut().expect("pipeline metadata object").insert("requested_attachments".into(),
                serde_json::Value::Array(handle.requested_attachments.iter().map(|a|serde_json::json!({"format":a.format,"write_mask":a.write_mask})).collect()));
            if let Some(blends)=handle.requested_attachment_blends.as_ref() {
                let requested:Vec<_>=blends.iter().map(|b| {
                    serde_json::json!({"version":b.version,"enabled":b.enabled,"rgb_op":b.rgb_op,
                        "rgb_src":b.rgb_src,"rgb_dst":b.rgb_dst,"alpha_op":b.alpha_op,
                        "alpha_src":b.alpha_src,"alpha_dst":b.alpha_dst,"write_mask":b.write_mask})
                }).collect();
                meta.as_object_mut().expect("pipeline metadata object").insert("requested_attachment_blends".into(),serde_json::Value::Array(requested));
            }
        }
        if tess {
            meta.as_object_mut().expect("pipeline metadata object").insert("tessellation_emulation".into(),
                serde_json::json!({"vertex_stage":"tessellation_evaluation","expanded_stages":pipeline.stages.len()}));
        }
        let encoded=serde_json::to_vec(&meta).map_err(|e|e.to_string())?;
        Ok((vertex.spirv_bytes(),fragment.spirv_bytes(),encoded))
}
/// Resource policy of one native gate. Old gates keep `None`; the texture gate
/// admits only the bounded fragment texture0/sampler0 contract below.
#[derive(Clone,Copy,PartialEq,Eq)]
enum NativeResources {None,FragmentTexture0Sampler0,FixedUiR80Buffers,FixedUiR85Texture}
/// Current native render backend contract. Expand alongside actual encoder and
/// descriptor support, never silently discard translator requirements.
fn native_render_check(p: &metal2vulkan::pipeline::RenderPipeline, format:u32,allow_vertex_fetch:bool,expected_blend:Option<metal2vulkan::pipeline::BlendState>) -> Result<(),String> {
    native_render_check_policy(p,format,allow_vertex_fetch,expected_blend,NativeResources::None)
}
/// Exact translator facts of one runtime float texture2d<sample> at FS
/// [[texture(0)]] through runtime [[sampler(0)]], combined at set0/binding0.
/// Typed constants owned by the bridge; compared with the pipeline's own
/// metadata, never with caller-provided or re-serialized JSON.
struct TextureContract {
    binding:metal2vulkan::pipeline::DescriptorBinding,
    slots:Vec<(metal2vulkan::pipeline::SlotRef,metal2vulkan::pipeline::SlotInterface)>,
    dependencies:Vec<metal2vulkan::pipeline::RuntimeDependency>,
    constants:Vec<metal2vulkan::pipeline::SpecConstant>,
    notes:Vec<metal2vulkan::pipeline::PrecisionNote>,
    specialized:Vec<metal2vulkan::pipeline::SpecializedNote>,
}
fn texture_contract()->Result<TextureContract,String> {
    use serde_json::{from_value,json};
    let texture=json!({"class":"Texture","index":0,"stage":"Fragment"});
    let sampler=json!({"class":"Sampler","index":0,"stage":"Fragment"});
    let e=|e:serde_json::Error|e.to_string();
    let test=|mask:u32,equal:bool|json!({"key":{"SamplerState":{"slot":sampler,"fields":0x8000_0007u32}},
        "mask":mask,"bits":0,"equal":equal});
    Ok(TextureContract{
        binding:from_value(json!({"binding":0,"ty":1,"count":1,"stages":16,"flags":0,"immutable_sampler":null,
            "source":{"CombinedTextureSampler":{"texture":texture,"sampler":{"compare":false,"cube_gradient":false,
            "integer":false,"raw":false,"sampler":{"Slot":0}}}}})).map_err(e)?,
        slots:from_value(json!([
            [texture,{"Texture":{"texture_type":"Type2D","sampled_type":"Float","depth_compare":false,
                "access":{"sample":true,"read":false,"write":false}}}],
            [sampler,{"Sampler":{"compare":false,"coordinate_sensitive":true}}]])).map_err(e)?,
        dependencies:from_value(json!([
            {"fact":{"SamplerCoordinates":{"slot":sampler}},"mechanism":"Specialization"},
            {"fact":{"SamplerReduction":{"slot":sampler}},"mechanism":"PlanTimeCheck"},
            {"fact":{"SrgbTexture":{"slot":texture}},"mechanism":"Variant"},
            {"fact":{"YCbCrTexture":{"slot":texture}},"mechanism":"Variant"},
            {"fact":{"CubicSampler":{"slot":sampler}},"mechanism":"Variant"},
            {"fact":{"CrossClassReads":{"slot":texture}},"mechanism":"Variant"}])).map_err(e)?,
        // Default 0 = normalized coordinates, nearest min/mag filters.
        constants:from_value(json!([{"id":0,"size":4,"default":0,"unread_bits":0xffff_fff8u32,
            "key":{"SamplerState":{"slot":sampler,"fields":0x8000_0007u32}}}])).map_err(e)?,
        notes:from_value(json!(["PixelSamplerScaling","DeviceSampling","DeviceTexelConversion"])).map_err(e)?,
        // Typed-only (absent from metadata JSON): pixel-coordinate samplers
        // with mixed min/mag filters. Must not hold at the default 0 below.
        specialized:from_value(json!([{"note":"PixelSamplerScaling","when":[
            {"all":[test(1,false),test(2,false),test(4,true)]},
            {"all":[test(1,false),test(2,true),test(4,false)]}]}])).map_err(e)?,
    })
}
/// Whether a specialized note's condition holds when every constant keeps
/// its default; unknown keys count as holding.
fn specialized_holds_at_default(note:&metal2vulkan::pipeline::SpecializedNote,
 constants:&[metal2vulkan::pipeline::SpecConstant])->bool {
    note.when.iter().any(|clause|clause.all.iter().all(|t|constants.iter().find(|c|c.key==t.key)
        .is_none_or(|c|((c.default as u32)&t.mask==t.bits)==t.equal)))
}
/// Exact resource facts for the bounded texture gate; false means refuse.
fn texture_resources_match(p:&metal2vulkan::pipeline::RenderPipeline,c:&TextureContract)->bool {
    let sets=&p.layout.sets;
    sets.len()==1 && sets[0].set==0 && sets[0].flags.as_raw()==0 &&
    sets[0].bindings.len()==1 && sets[0].bindings[0]==c.binding &&
    p.interface.slots.len()==c.slots.len() &&
    p.interface.slots.iter().zip(&c.slots).all(|((k,v),(ek,ev))|k==ek&&v==ev) &&
    p.dependencies==c.dependencies
}
/// R80's fixed BackBoard UI pair has two caller-owned uniform-word buffers and
/// one synthetic readonly constant-blob binding, with no textures. Compare the
/// translator's typed metadata before letting the native gate proceed.
fn fixed_ui_r80_resources_match(p:&metal2vulkan::pipeline::RenderPipeline)->bool {
    use metal2vulkan::pipeline::{AccessSet,BufferForm,BufferKind,DescriptorView,
        SlotInterface,SlotRef,Window};
    use serde_json::json;
    let bindings=json!([
        {"binding":0,"count":1,"flags":0,"immutable_sampler":null,
         "source":{"Buffer":{"class":"Buffer","index":2,"stage":"Vertex"}},"stages":1,"ty":6},
        {"binding":1,"count":1,"flags":0,"immutable_sampler":null,
         "source":{"Buffer":{"class":"Buffer","index":1,"stage":"Fragment"}},"stages":16,"ty":6},
        {"binding":2,"count":1,"flags":0,"immutable_sampler":null,
         "source":{"Synthetic":{"binding":"ConstantBlob","stage":"Fragment"}},"stages":16,"ty":7}
    ]);
    let actual_sets=serde_json::to_value(&p.layout.sets).ok();
    let bounded_uniform=|stage,index,len| {
        let key=SlotRef{stage,class:SlotClass::Buffer,index};
        match p.interface.slots.get(&key) {
            Some(SlotInterface::Buffer(slot))=>slot.access==AccessSet::READ &&
                slot.window==Window::Bounded{start:0,len} &&
                slot.form==BufferForm::Descriptor(DescriptorView::UniformWords) &&
                slot.kind==BufferKind::Buffer,
            _=>false,
        }
    };
    if p.interface.slots.len()!=2 ||
       !bounded_uniform(Stage::Vertex,2,64) || !bounded_uniform(Stage::Fragment,1,168) ||
       !fixed_ui_r80_layout_values_match(actual_sets.as_ref(),&bindings) ||
       p.layout.sets.len()!=1 || p.layout.sets[0].set!=0 || p.layout.sets[0].flags.as_raw()!=0 {
        return false;
    }
    // These AIR fields are the only runtime binding offsets in this class.
    // Keep the reflected ranges exact as well as checking the three locations.
    if p.interface.push.len()!=2 {return false;}
    let mut push_fields=Vec::new();
    for placement in &p.interface.push {
        let expected_range=match placement.stage {
            Stage::Vertex=>Some((0,4)),
            Stage::Fragment=>Some((64,8)),
            _=>return false,
        };
        if placement.argument_table_size!=0 || placement.push_range!=expected_range {return false;}
        for field in &placement.fields {
            let offset=match field.loc {metal2vulkan::pipeline::PushLoc::Push{offset}=>offset,_=>return false};
            let valid=match field.field {
                metal2vulkan::pipeline::PushField::SlotBindOffset(slot)=>
                    fixed_ui_r80_push_field_matches(offset,slot.stage,slot.class,slot.index),
                metal2vulkan::pipeline::PushField::ColorWriteRule(0)=>
                    offset==68&&placement.stage==Stage::Fragment,
                _=>false,
            };
            if field.size!=4 || !valid {return false;}
            push_fields.push(offset);
        }
    }
    if !fixed_ui_r80_push_offsets_match(&mut push_fields) || !p.dependencies.is_empty() ||
       p.interface.texture_writing_stages.len()!=0 || p.interface.needs_pointer_translation ||
       p.interface.needs_handle_translation || p.interface.reaches_command_buffers ||
       !p.interface.threadgroup.is_empty() {
        return false;
    }
    let samplers=serde_json::to_value(&p.interface.numbered_samplers).ok();
    if samplers.as_ref()!=Some(&json!([
        ["Vertex",[]],
        ["Fragment",[
            {"Constexpr":[34901797601017929u64,0]},
            {"Constexpr":[34901797601020489u64,0]},
            {"Constexpr":[34901797601018002u64,0]}
        ]]
    ])){return false;}
    let constants:Vec<_>=p.stages.iter().filter_map(|s|s.constant_blob.as_ref()).collect();
    constants.len()==1 && constants[0].as_ref()==[0,126,0,126,0,126,0,126]
}
fn fixed_ui_r80_layout_values_match(actual_sets:Option<&serde_json::Value>,
 expected_bindings:&serde_json::Value)->bool {
    actual_sets.and_then(|v|v.as_array()).and_then(|a|a.first())
      .and_then(|v|v.get("bindings"))==Some(expected_bindings)
}
fn fixed_ui_r80_push_offsets_match(offsets:&mut Vec<u32>)->bool {
    offsets.sort_unstable();offsets.as_slice()==[0,64,68]
}
fn fixed_ui_r80_push_field_matches(offset:u32,stage:Stage,class:SlotClass,index:u32)->bool {
    match offset {
        0=>stage==Stage::Vertex&&class==SlotClass::Buffer&&index==2,
        64|68=>stage==Stage::Fragment&&class==SlotClass::Buffer&&index==1,
        _=>false,
    }
}
/// Exact non-resource stage facts captured for the fixed UI pair. The native
/// compiler independently pins both SPIR-V hashes and all Mali output facts.
fn fixed_ui_r80_stages_match(p:&metal2vulkan::pipeline::RenderPipeline)->bool {
    use metal2vulkan::pipeline::Reconvergence;
    use serde_json::json;
    if p.stages.len()!=2 {return false;}
    let common=|stage:&metal2vulkan::pipeline::StageModule,expected| stage.stage==expected &&
        stage.entry_point=="main" && stage.specialization.constants.is_empty() &&
        stage.required_subgroup_size.is_none() && stage.stage_flags.as_raw()==0 &&
        stage.specialized_notes.is_empty() && stage.half_bit_colors.is_empty();
    let vertex=&p.stages[0];
    let fragment=&p.stages[1];
    common(vertex,vk::ShaderStage::VERTEX) && vertex.constant_blob.is_none() &&
      vertex.rounded_colors.is_empty() && vertex.reconvergence==Reconvergence::Guaranteed &&
      serde_json::to_value(&vertex.precision_notes).ok()==Some(json!([
        "NativeFma","NativeHalfQuantize","NativeNanEncoding"
      ])) &&
    common(fragment,vk::ShaderStage::FRAGMENT) &&
      fragment.constant_blob.as_deref()==Some(&[0,126,0,126,0,126,0,126][..]) &&
      fragment.rounded_colors==[0] && fragment.reconvergence==Reconvergence::ImplementationDefined &&
      serde_json::to_value(&fragment.precision_notes).ok()==Some(json!([
        "DeviceTranscendentals","DeviceDenormals","DeviceArithmetic","DeviceSignedZeroInfNan",
        "FastMathRewrites","FunctionStorageAssumed","HalfOutputFastMathBlend",
        "NativeTranscendentals","NativeDivision","NativeFma","NativeHalfQuantize",
        "NativeNanEncoding","NativeMinMax","NativeDot","NativeMix"
      ]))
}
fn fixed_ui_r80_requirements_match(p:&metal2vulkan::pipeline::RenderPipeline)->bool {
    use metal2vulkan::device::{ApiVersion,Independence,VkFeature,VkLimit};
    let r=&p.requirements;
    r.api_version==Some(ApiVersion::V1_2) &&
      r.spirv_capabilities.iter().map(|c|c.as_raw()).collect::<Vec<_>>()==[1,51] &&
      r.spirv_extensions.is_empty() && r.features.len()==1 &&
      r.features.contains(&VkFeature::UniformBufferStandardLayout) &&
      r.extensions.is_empty() && r.properties.is_empty() && r.limits.len()==1 &&
      r.limits.get(&VkLimit::MaxPushConstantsSize)==Some(&72) &&
      r.formats.is_empty() && r.stage_flags.is_empty() &&
      r.denorm_independence==Independence::None && r.rounding_independence==Independence::None
}
/// Exact resource contract of the sampled BackBoard fixed-UI specialization
/// first admitted by R85.  This is deliberately separate from the generic
/// texture gate: its texture lives at FS slot 3 and four bounded uniform
/// windows plus a stage constant are part of the same indivisible contract.
fn fixed_ui_r85_resources_match(p:&metal2vulkan::pipeline::RenderPipeline)->bool {
    use serde_json::json;
    let slots=json!([
      [{"class":"Buffer","index":2,"stage":"Vertex"},{"Buffer":{"access":{"read":true,"write":false},"form":{"Descriptor":"UniformWords"},"kind":"Buffer","window":{"Bounded":{"len":64,"start":0}}}}],
      [{"class":"Buffer","index":3,"stage":"Vertex"},{"Buffer":{"access":{"read":true,"write":false},"form":{"Descriptor":"UniformWords"},"kind":"Buffer","window":{"Bounded":{"len":16,"start":0}}}}],
      [{"class":"Buffer","index":1,"stage":"Fragment"},{"Buffer":{"access":{"read":true,"write":false},"form":{"Descriptor":"UniformWords"},"kind":"Buffer","window":{"Bounded":{"len":168,"start":0}}}}],
      [{"class":"Buffer","index":2,"stage":"Fragment"},{"Buffer":{"access":{"read":true,"write":false},"form":{"Descriptor":"UniformWords"},"kind":"Buffer","window":{"Bounded":{"len":8,"start":0}}}}],
      [{"class":"Texture","index":3,"stage":"Fragment"},{"Texture":{"access":{"read":false,"sample":true,"write":false},"depth_compare":false,"sampled_type":"Float","texture_type":"Type2D"}}],
      [{"class":"Sampler","index":0,"stage":"Fragment"},{"Sampler":{"compare":false,"coordinate_sensitive":true}}]
    ]);
    let bindings=json!([
      {"binding":0,"count":1,"flags":0,"immutable_sampler":null,"source":{"Buffer":{"class":"Buffer","index":2,"stage":"Vertex"}},"stages":1,"ty":6},
      {"binding":1,"count":1,"flags":0,"immutable_sampler":null,"source":{"Buffer":{"class":"Buffer","index":3,"stage":"Vertex"}},"stages":1,"ty":6},
      {"binding":2,"count":1,"flags":0,"immutable_sampler":null,"source":{"Buffer":{"class":"Buffer","index":1,"stage":"Fragment"}},"stages":16,"ty":6},
      {"binding":3,"count":1,"flags":0,"immutable_sampler":null,"source":{"Buffer":{"class":"Buffer","index":2,"stage":"Fragment"}},"stages":16,"ty":6},
      {"binding":4,"count":1,"flags":0,"immutable_sampler":null,"source":{"Synthetic":{"binding":"ConstantBlob","stage":"Fragment"}},"stages":16,"ty":7},
      {"binding":5,"count":1,"flags":0,"immutable_sampler":null,"source":{"CombinedTextureSampler":{"sampler":{"compare":false,"cube_gradient":false,"integer":false,"raw":false,"sampler":{"Slot":0}},"texture":{"class":"Texture","index":3,"stage":"Fragment"}}},"stages":16,"ty":1}
    ]);
    let dependencies=json!([
      {"fact":{"SamplerCoordinates":{"slot":{"class":"Sampler","index":0,"stage":"Fragment"}}},"mechanism":"Specialization"},
      {"fact":{"SamplerReduction":{"slot":{"class":"Sampler","index":0,"stage":"Fragment"}}},"mechanism":"PlanTimeCheck"},
      {"fact":{"SrgbTexture":{"slot":{"class":"Texture","index":3,"stage":"Fragment"}}},"mechanism":"Variant"},
      {"fact":{"YCbCrTexture":{"slot":{"class":"Texture","index":3,"stage":"Fragment"}}},"mechanism":"Variant"},
      {"fact":{"CubicSampler":{"slot":{"class":"Sampler","index":0,"stage":"Fragment"}}},"mechanism":"Variant"},
      {"fact":{"CrossClassReads":{"slot":{"class":"Texture","index":3,"stage":"Fragment"}}},"mechanism":"Variant"}
    ]);
    let numbered=json!([["Vertex",[]],["Fragment",[
      {"Constexpr":[34901797601017929u64,0]},
      {"Constexpr":[34901797601020489u64,0]},
      {"Constexpr":[34901797601018002u64,0]}
    ]]]);
    let expected_slots:Vec<(metal2vulkan::pipeline::SlotRef,metal2vulkan::pipeline::SlotInterface)>=
      match serde_json::from_value(slots){Ok(value)=>value,Err(_)=>return false};
    if p.interface.slots.len()!=expected_slots.len()||
       !p.interface.slots.iter().zip(&expected_slots).all(|((slot,value),(expected_slot,expected_value))|
         slot==expected_slot&&value==expected_value) ||
       serde_json::to_value(&p.layout.sets).ok()!=Some(json!([{"bindings":bindings,"flags":0,"set":0}])) ||
       serde_json::to_value(&p.dependencies).ok()!=Some(dependencies) ||
       serde_json::to_value(&p.interface.numbered_samplers).ok()!=Some(numbered) ||
       serde_json::to_value(&p.layout.push_constants).ok()!=Some(json!([
         {"offset":0,"size":8,"stages":1},{"offset":64,"size":12,"stages":16}
       ])) || p.layout.push_descriptor_eligible || p.interface.push.len()!=2 ||
       !p.interface.texture_writing_stages.is_empty() || !p.interface.threadgroup.is_empty() ||
       p.interface.needs_pointer_translation || p.interface.needs_handle_translation ||
       p.interface.reaches_command_buffers {return false;}
    let mut fields=Vec::new();
    for placement in &p.interface.push {
      let expected=match placement.stage {Stage::Vertex=>(0,8),Stage::Fragment=>(64,12),_=>return false};
      if placement.argument_table_size!=0||placement.push_range!=Some(expected){return false;}
      for field in &placement.fields {
        let offset=match field.loc {metal2vulkan::pipeline::PushLoc::Push{offset}=>offset,_=>return false};
        let valid=match field.field {
          metal2vulkan::pipeline::PushField::SlotBindOffset(slot)=>fixed_ui_r85_push_field_matches(offset,slot.stage,slot.class,slot.index),
          metal2vulkan::pipeline::PushField::ColorWriteRule(0)=>offset==72&&placement.stage==Stage::Fragment,
          _=>false,
        };
        if field.size!=4||!valid{return false;} fields.push(offset);
      }
    }
    fields.sort_unstable();fields==[0,4,64,68,72]
}
fn fixed_ui_r85_push_field_matches(offset:u32,stage:Stage,class:SlotClass,index:u32)->bool {
    class==SlotClass::Buffer && matches!((offset,stage,index),
      (0,Stage::Vertex,2)|(4,Stage::Vertex,3)|(64,Stage::Fragment,1)|(68,Stage::Fragment,2))
}
fn fixed_ui_r85_stages_match(p:&metal2vulkan::pipeline::RenderPipeline)->bool {
    use metal2vulkan::pipeline::Reconvergence;
    use serde_json::json;
    if p.stages.len()!=2{return false;}
    let vertex=&p.stages[0];let fragment=&p.stages[1];
    let common=|s:&metal2vulkan::pipeline::StageModule,stage|s.stage==stage&&s.entry_point=="main"&&
      s.required_subgroup_size.is_none()&&s.stage_flags.as_raw()==0&&s.half_bit_colors.is_empty();
    let mut sampler_notes=match texture_contract(){Ok(contract)=>contract.specialized,Err(_)=>return false};
    let texture=json!({"class":"Texture","index":3,"stage":"Fragment"});
    let nan_note=match serde_json::from_value(json!({"note":"DeviceNanEncoding","when":[{"all":[{
      "key":{"BoundTexelFormat":texture},"mask":3078,"bits":0,"equal":true}]}]})){
      Ok(note)=>note,Err(_)=>return false};
    sampler_notes.push(nan_note);
    let matched=common(vertex,vk::ShaderStage::VERTEX)&&vertex.specialization.constants.is_empty()&&vertex.constant_blob.is_none()&&
      vertex.specialized_notes.is_empty()&&vertex.rounded_colors.is_empty()&&vertex.reconvergence==Reconvergence::Guaranteed&&
      serde_json::to_value(&vertex.precision_notes).ok()==Some(json!(["NativeFma","NativeHalfQuantize","NativeNanEncoding"]))&&
    common(fragment,vk::ShaderStage::FRAGMENT)&&fragment.specialized_notes==sampler_notes&&
      !specialized_holds_at_default(&fragment.specialized_notes[0],&fragment.specialization.constants)&&
      fragment.constant_blob.as_deref()==Some(&[0,126,0,126,0,126,0,126][..])&&
      fragment.rounded_colors==[0]&&fragment.reconvergence==Reconvergence::ImplementationDefined&&
      serde_json::to_value(&fragment.specialization.constants).ok()==Some(json!([{"default":0,"id":0,
        "key":{"SamplerState":{"fields":2147483655u32,"slot":{"class":"Sampler","index":0,"stage":"Fragment"}}},
        "size":4,"unread_bits":4294967288u32}]))&&
      serde_json::to_value(&fragment.precision_notes).ok()==Some(json!([
        "DeviceTranscendentals","DeviceDenormals","PixelSamplerScaling","DeviceArithmetic",
        "DeviceSignedZeroInfNan","FastMathRewrites","DeviceNanEncoding","HalfTexelConversion",
        "FunctionStorageAssumed","HalfOutputFastMathBlend","DeviceSampling","NativeTranscendentals",
        "NativeDivision","NativeFma","NativeHalfQuantize","NativeNanEncoding","NativeMinMax","NativeDot","NativeMix"
      ]));
    matched
}
fn fixed_ui_r85_requirements_match(p:&metal2vulkan::pipeline::RenderPipeline)->bool {
    use metal2vulkan::device::{ApiVersion,Independence,VkFeature,VkLimit,VkProperty};
    let r=&p.requirements;
    r.api_version==Some(ApiVersion::V1_2)&&
      r.spirv_capabilities.iter().map(|c|c.as_raw()).collect::<Vec<_>>()==[1,50,51]&&
      r.spirv_extensions.is_empty()&&r.features.len()==1&&r.features.contains(&VkFeature::UniformBufferStandardLayout)&&
      r.extensions.is_empty()&&r.properties.len()==1&&r.properties.contains(&VkProperty::DeviceSampling)&&
      r.limits.len()==1&&r.limits.get(&VkLimit::MaxPushConstantsSize)==Some(&76)&&
      r.formats.is_empty()&&r.stage_flags.is_empty()&&r.denorm_independence==Independence::None&&
      r.rounding_independence==Independence::None
}
fn native_render_check_policy(p: &metal2vulkan::pipeline::RenderPipeline, format:u32,allow_vertex_fetch:bool,
 expected_blend:Option<metal2vulkan::pipeline::BlendState>,resources:NativeResources) -> Result<(),String> {
    use metal2vulkan::device::{VkLimit,VkProperty,Independence};
    let texture=resources==NativeResources::FragmentTexture0Sampler0;
    let fixed_ui_r80=resources==NativeResources::FixedUiR80Buffers;
    let fixed_ui_r85=resources==NativeResources::FixedUiR85Texture;
    let contract=if texture {Some(texture_contract()?)} else {None};
    let expected=match format {10=>9,70=>37,80=>44,_=>return Err("unsupported native color format".into())};
    let g=&p.graphics;
    if g.topology.as_raw()!=3 || g.rasterization_samples.as_raw()!=1 ||
       g.color_attachments.len()!=1 || g.color_outputs!=1 || g.depth_format.is_some() ||
       g.stencil_format.is_some() || g.alpha_to_coverage || g.alpha_to_one || g.rasterizer_discard ||
       !g.input_attachments.is_empty() || g.rasterization_order_attachment_access ||
       g.max_vertex_amplification_count!=1 || g.declares_layer || g.tessellation.is_some() ||
       g.fragment_imageblock.is_some() || g.sample_shading || g.vertex_writes_memory ||
       g.writes_viewport_index || !g.storage_attachments.is_empty() || g.shadow_stencil.is_some() ||
       g.early_fragment_tests || g.mesh_records.is_some() || g.writes_point_size ||
       g.flat_fragment_inputs || g.clip_distances!=0 || g.writes_depth ||
       g.reads_barycentric_coord || g.reads_position_depth || g.fragment_writes_memory ||
       g.sample_mask.is_some() || g.shader_tests.is_some() {
        return Err("native render graphics state unsupported".into());
    }
    // These are viewport, scissor, depth bias, blend constants and stencil
    // masks/reference. The encoder must supply their non-depth/stencil defaults.
    if g.dynamic_states.iter().any(|s|![0,1,3,4,6,7,8].contains(&s.as_raw())) {
        return Err("native render dynamic state unsupported".into());
    }
    let color=g.color_attachments[0].as_ref().ok_or("missing native color attachment")?;
    if color.format.as_raw()!=expected || color.write_mask.as_raw()!=15 || color.blend!=expected_blend {
        return Err("native render color attachment unsupported".into());
    }
    let v=&p.vertex_input;let interface=&p.interface;
    if allow_vertex_fetch {render_vertex_fetch(p)?;}
    let resource_contract_matches=if fixed_ui_r80 {fixed_ui_r80_resources_match(p)}
      else if fixed_ui_r85 {fixed_ui_r85_resources_match(p)} else {false};
    let unbound=match &contract {
        None if fixed_ui_r80||fixed_ui_r85=>!resource_contract_matches,
        None=>!p.dependencies.is_empty() || !interface.slots.is_empty() ||
            p.layout.sets.iter().any(|s|!s.bindings.is_empty() || s.flags.as_raw()!=0),
        Some(_)=>false,
    };
    // Keep these refusals separate.  A single aggregate error made real-device
    // shader contracts impossible to distinguish without exporting donor AIR.
    if !allow_vertex_fetch&&(!v.attributes.is_empty() || !v.bindings.is_empty()) {
        return Err("native render static vertex input unsupported".into());
    }
    if !v.pulled.is_empty() {return Err("native render pulled vertex input unsupported".into());}
    if !v.variant_pulled.is_empty() {return Err("native render variant vertex input unsupported".into());}
    if unbound {return Err("native render unbound resources unsupported".into());}
    if p.variant_bits!=0 {return Err("native render resource variants unsupported".into());}
    if !interface.threadgroup.is_empty() {return Err("native render threadgroup interface unsupported".into());}
    if interface.needs_pointer_translation {return Err("native render pointer translation unsupported".into());}
    if interface.needs_handle_translation {return Err("native render handle translation unsupported".into());}
    if interface.reaches_command_buffers {return Err("native render command-buffer interface unsupported".into());}
    if !interface.texture_writing_stages.is_empty() {return Err("native render texture writes unsupported".into());}
    if interface.numbered_samplers.iter().any(|(_,s)|!s.is_empty()) &&
       !((fixed_ui_r80||fixed_ui_r85)&&resource_contract_matches) {
        return Err("native render numbered samplers unsupported".into());
    }
    if let Some(c)=&contract {
        if !texture_resources_match(p,c) {return Err("native texture resources differ from bounded contract".into());}
    }
    if p.layout.push_constants.iter().any(|r|r.offset.checked_add(r.size).is_none_or(|end|end>128) ||
       r.stages.as_raw() & !17 != 0) {
        return Err("native render push layout unsupported".into());
    }
    if p.stages.len()!=2 {return Err("native render needs vertex and fragment stages".into());}
    // For real RGBA8/BGRA8 blended attachments the translator intentionally
    // leaves conversion to the native fixed-function unit. Every other
    // precision note remains refused; replacement paths still use the shader's
    // rounding rules.
    let native_blend_conversion=matches!(format,70|80)&&expected_blend.is_some();
    if fixed_ui_r80||fixed_ui_r85 {
        let stages_match=if fixed_ui_r80 {fixed_ui_r80_stages_match(p)} else {fixed_ui_r85_stages_match(p)};
        if !resource_contract_matches || !stages_match {
            return Err("native render stage requirements unsupported".into());
        }
    } else { for (stage,expected) in p.stages.iter().zip([vk::ShaderStage::VERTEX,vk::ShaderStage::FRAGMENT]) {
        // Only the texture gate's fragment stage carries the sampler spec
        // constant, sampling notes and its one rounded color target.
        let sampled=contract.as_ref().filter(|_|expected==vk::ShaderStage::FRAGMENT);
        let (constants_ok,notes_ok,rounded_ok)=match sampled {
            Some(c)=>(stage.specialization.constants==c.constants,
                stage.precision_notes==c.notes&&stage.specialized_notes==c.specialized&&
                !stage.specialized_notes.iter().any(|n|specialized_holds_at_default(n,&c.constants)),
                stage.rounded_colors==[0]),
            None=>(stage.specialization.constants.is_empty()&&stage.specialized_notes.is_empty(),
                stage.precision_notes.iter().all(|note|native_blend_conversion&&expected==vk::ShaderStage::FRAGMENT&&
                    *note==metal2vulkan::pipeline::PrecisionNote::DeviceColorConversion),
                stage.rounded_colors.iter().all(|c|expected==vk::ShaderStage::FRAGMENT && *c==0)),
        };
        if stage.stage!=expected || stage.entry_point!="main" || !constants_ok ||
           stage.required_subgroup_size.is_some() || stage.stage_flags.as_raw()!=0 || stage.constant_blob.is_some() ||
           !rounded_ok || !stage.half_bit_colors.is_empty() || !notes_ok ||
           stage.reconvergence!=metal2vulkan::pipeline::Reconvergence::Guaranteed {
            return Err("native render stage requirements unsupported".into());
        }
    }}
    let r=&p.requirements;
    // Texture gate: exactly Shader+ImageQuery and the DeviceSampling property.
    if fixed_ui_r80||fixed_ui_r85 {
        let requirements_match=if fixed_ui_r80 {fixed_ui_r80_requirements_match(p)} else {fixed_ui_r85_requirements_match(p)};
        if !requirements_match {
            return Err("native render shader capability unsupported".into());
        }
        return Ok(());
    }
    let (capabilities_ok,properties_ok)=if texture {
        (r.spirv_capabilities.iter().map(|c|c.as_raw()).collect::<Vec<_>>()==[1,50],
         r.properties.len()==1&&r.properties.contains(&VkProperty::DeviceSampling))
    } else {(r.spirv_capabilities.iter().all(|c|c.as_raw()==1),r.properties.is_empty())};
    if !capabilities_ok || !r.spirv_extensions.is_empty() ||
       !r.features.is_empty() || !r.extensions.is_empty() || !properties_ok ||
       !r.formats.is_empty() || !r.stage_flags.is_empty() ||
       r.denorm_independence!=Independence::None || r.rounding_independence!=Independence::None ||
       r.limits.iter().any(|(k,v)|*k!=VkLimit::MaxPushConstantsSize || *v>128) {
        return Err("native render shader capability unsupported".into());
    }
    Ok(())
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check(
 pipeline:*const RenderPipelineHandle,format:u32,error:*mut c_char,cap:usize)->i32 {
    match unsafe {attempt(error,cap,|| {
        if pipeline.is_null() {return Err("null render pipeline".into());}
        if (*pipeline).source_blend.is_some_and(|b|b.enabled!=0){return Err("native render blend requires explicit gate".into());}
        native_render_check(&(*pipeline)._pipeline,format,false,None)
    })} {Some(())=>0,None=>-1}
}

/// Resource-facing translator state retained by the opaque pipeline. Keep this
/// independent of native_render_check: observing a contract must never widen
/// or cache an execution decision. JSON preserves the translator's typed,
/// non-exhaustive slot/dependency enums without creating a lossy parallel ABI.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_resource_contract(
 pipeline:*const RenderPipelineHandle,out:*mut Blob,error:*mut c_char,cap:usize)->i32 {
 let oi=out as usize;let ei=error as usize;let pi=pipeline as usize;
 let pn=if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()};
 let error_aliases_pipeline=compositor_ranges_overlap(ei,cap,pi,pn);
 if out.is_null(){
  if !error_aliases_pipeline{unsafe{error_copy(error,cap,"null render resource contract output")};}
  return -1;
 }
 if compositor_ranges_overlap(oi,size_of::<Blob>(),ei,cap)||
    compositor_ranges_overlap(oi,size_of::<Blob>(),pi,pn)||error_aliases_pipeline{return -1;}
 unsafe{*out=Blob{bytes:ptr::null_mut(),length:0};}
 match unsafe{attempt(error,cap,||{
  if pipeline.is_null(){return Err("null render pipeline resource contract".into());}
  let p=&(*pipeline)._pipeline;
  serde_json::to_vec(&serde_json::json!({
   "schema":1,
   "scope":"translator resource contract diagnostic; not native execution approval",
   "layout":p.layout,
   "interface":p.interface,
   "dependencies":p.dependencies,
   "variant_bits":p.variant_bits
  })).map_err(|e|e.to_string())
 })}{Some(encoded)=>{unsafe{*out=blob(encoded)};0},None=>-1}
}

/// Exact bounded captured compositor metadata contract. This compares the full
/// translator result (graphics state, requirements, interface, dependencies,
/// layout, vertex inputs and stage metadata); it is not an execution approval.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_compositor(
 pipeline:*const RenderPipelineHandle,error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null compositor pipeline".into());}
  let handle=&*pipeline;let p=&handle._pipeline;
  let validation=handle._device.profile().spirv_validation();
  let stages:Vec<_>=p.stages.iter().map(|s|serde_json::json!({
   "stage":s.stage,"entry_point":s.entry_point,"specialization":s.specialization,
   "required_subgroup_size":s.required_subgroup_size,"stage_flags":s.stage_flags,
   "constant_blob":s.constant_blob.as_ref().map(|b|b.to_vec()),
   "rounded_colors":s.rounded_colors,"half_bit_colors":s.half_bit_colors,
   "precision_notes":s.precision_notes,"reconvergence":s.reconvergence
  })).collect();
  let mut actual=serde_json::json!({"schema":1,"scope":"render_compilation_not_native_draw_plan",
   "color_format":0,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},
   "profile":p.profile,"stages":stages,"requirements":{
    "api_version":p.requirements.api_version,"spirv_capabilities":p.requirements.spirv_capabilities,
    "spirv_extensions":p.requirements.spirv_extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "features":p.requirements.features,"extensions":p.requirements.extensions,
    "properties":p.requirements.properties.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "limits":p.requirements.limits.iter().map(|(k,v)|(format!("{k:?}"),*v)).collect::<std::collections::BTreeMap<_,_>>(),
    "formats":p.requirements.formats.iter().map(|(f,u)|(f,format!("{u:?}"))).collect::<Vec<_>>(),
    "stage_flags":p.requirements.stage_flags.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "denorm_independence":p.requirements.denorm_independence,"rounding_independence":p.requirements.rounding_independence},
   "layout":p.layout,"interface":p.interface,"dependencies":p.dependencies,
   "vertex_input":p.vertex_input,"graphics":p.graphics,"variant_bits":p.variant_bits});
  let attachments:Vec<_>=handle.requested_attachments.iter().map(|a|serde_json::json!({"format":a.format,"write_mask":a.write_mask})).collect();
  actual.as_object_mut().ok_or("pipeline metadata is not an object")?.insert("requested_attachments".into(),serde_json::Value::Array(attachments));
  /* Accept qualified fixed fetch records for Metal slots0/1 and strides32/40/48.
   * Normalize only the comparison copy; the pipeline metadata itself retains the
   * original slot and stride for package creation and per-draw binding. */
  let vi=actual.get_mut("vertex_input").ok_or("missing compositor vertex metadata")?;
  let attribute_count=vi.get("attributes").and_then(|v|v.as_array()).ok_or("missing fixed compositor attributes")?.len();
  let pulled_empty=vi.get("pulled").and_then(|v|v.as_array()).ok_or("missing compositor pull rows")?.is_empty();
  let bindings=vi.get_mut("bindings").and_then(|v|v.as_array_mut()).ok_or("missing compositor vertex bindings")?;
  if attribute_count!=2||bindings.len()!=1||!pulled_empty{return Err("compositor vertex fetch is not exact fixed PerVertex class".into());}
  for row in bindings.iter_mut(){
   let stride=row.get("stride").and_then(|v|v.get("Static")).and_then(|v|v.as_u64()).ok_or("compositor vertex stride is not static")?;
   if !matches!(stride,32|40|48){return Err("compositor vertex stride outside bounded32/40/48 class".into());}
   let slot=row.get("buffer_index").and_then(|v|v.as_u64()).ok_or("missing Metal vertex buffer index")?;
   if slot>1{return Err("compositor Metal vertex buffer slot outside0/1 class".into());}
   row.as_object_mut().ok_or("invalid compositor vertex binding")?.insert("buffer_index".into(),serde_json::json!(0));
   row.as_object_mut().ok_or("invalid compositor vertex binding")?.insert("stride".into(),serde_json::json!({"Static":40}));
  }
  let mut expected:serde_json::Value=serde_json::from_str(include_str!("compositor_contract.json")).map_err(|e|e.to_string())?;
  if let Some(blends)=handle.requested_attachment_blends.as_ref().filter(|b|b.iter().any(|v|v.enabled!=0)) {
   let exact=[RenderBlend{version:1,enabled:1,rgb_op:0,rgb_src:1,rgb_dst:5,alpha_op:0,alpha_src:1,alpha_dst:5,write_mask:15},
    RenderBlend{version:1,enabled:0,rgb_op:0,rgb_src:1,rgb_dst:0,alpha_op:0,alpha_src:1,alpha_dst:0,write_mask:0}];
   if blends.as_slice()!=exact{return Err("compositor blend descriptor outside exact premultiplied source-over class".into());}
   let ca=expected.get_mut("graphics").and_then(|v|v.get_mut("color_attachments")).and_then(|v|v.as_array_mut()).ok_or("missing expected color attachments")?;
   ca[0].as_object_mut().ok_or("bad expected target0")?.insert("blend".into(),serde_json::json!({"alpha_op":0,"color_op":0,"dst_alpha":7,"dst_color":7,"src_alpha":1,"src_color":1}));
   let requested:Vec<_>=blends.iter().map(|b|serde_json::json!({"version":b.version,"enabled":b.enabled,"rgb_op":b.rgb_op,"rgb_src":b.rgb_src,"rgb_dst":b.rgb_dst,"alpha_op":b.alpha_op,"alpha_src":b.alpha_src,"alpha_dst":b.alpha_dst,"write_mask":b.write_mask})).collect();
   expected.as_object_mut().ok_or("bad expected metadata")?.insert("requested_attachment_blends".into(),serde_json::Value::Array(requested.clone()));
   actual.as_object_mut().ok_or("bad actual metadata")?.insert("requested_attachment_blends".into(),serde_json::Value::Array(requested));
  }
  if actual!=expected {
   let mut mismatches=Vec::new();
   for key in ["color_format","validation","profile","stages","requirements","layout","interface","dependencies","vertex_input","graphics","variant_bits","requested_attachments"] {
    if actual.get(key)!=expected.get(key){mismatches.push(format!("{key}: {:?} != {:?}",actual.get(key),expected.get(key)));}
   }
   return Err(format!("translator compositor metadata differs from captured contract: {}",mismatches.join(",")));
  }
  Ok(())
 })} {Some(())=>0,None=>-1}
}


/// Exact isolated v7 comparator for the measured 16-sample compositor class.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_compositor_v7(
 pipeline:*const RenderPipelineHandle,error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null compositor pipeline".into());}
  let handle=&*pipeline;let p=&handle._pipeline;
  let validation=handle._device.profile().spirv_validation();
  let stages:Vec<_>=p.stages.iter().map(|s|serde_json::json!({
   "stage":s.stage,"entry_point":s.entry_point,"specialization":s.specialization,
   "required_subgroup_size":s.required_subgroup_size,"stage_flags":s.stage_flags,
   "constant_blob":s.constant_blob.as_ref().map(|b|b.to_vec()),
   "rounded_colors":s.rounded_colors,"half_bit_colors":s.half_bit_colors,
   "precision_notes":s.precision_notes,"reconvergence":s.reconvergence
  })).collect();
  let mut actual=serde_json::json!({"schema":1,"scope":"render_compilation_not_native_draw_plan",
   "color_format":0,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},
   "profile":p.profile,"stages":stages,"requirements":{
    "api_version":p.requirements.api_version,"spirv_capabilities":p.requirements.spirv_capabilities,
    "spirv_extensions":p.requirements.spirv_extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "features":p.requirements.features,"extensions":p.requirements.extensions,
    "properties":p.requirements.properties.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "limits":p.requirements.limits.iter().map(|(k,v)|(format!("{k:?}"),*v)).collect::<std::collections::BTreeMap<_,_>>(),
    "formats":p.requirements.formats.iter().map(|(f,u)|(f,format!("{u:?}"))).collect::<Vec<_>>(),
    "stage_flags":p.requirements.stage_flags.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "denorm_independence":p.requirements.denorm_independence,"rounding_independence":p.requirements.rounding_independence},
   "layout":p.layout,"interface":p.interface,"dependencies":p.dependencies,
   "vertex_input":p.vertex_input,"graphics":p.graphics,"variant_bits":p.variant_bits});
  let attachments:Vec<_>=handle.requested_attachments.iter().map(|a|serde_json::json!({"format":a.format,"write_mask":a.write_mask})).collect();
  let blends=handle.requested_attachment_blends.as_ref().ok_or("missing requested compositor blends")?;
  if blends.len()!=2||blends[1]!=(RenderBlend{version:1,enabled:0,rgb_op:0,rgb_src:1,rgb_dst:0,alpha_op:0,alpha_src:1,alpha_dst:0,write_mask:0}) {return Err("8-tap compositor requires exact disabled target1 blend".into());}
  let source_over=blends[0]==RenderBlend{version:1,enabled:1,rgb_op:0,rgb_src:1,rgb_dst:5,alpha_op:0,alpha_src:1,alpha_dst:5,write_mask:15};
  let disabled=blends[0]==RenderBlend{version:1,enabled:0,rgb_op:0,rgb_src:1,rgb_dst:0,alpha_op:0,alpha_src:1,alpha_dst:0,write_mask:15};
  if !source_over&&!disabled {return Err("8-tap compositor target0 blend outside captured source-over or disabled classes".into());}
  actual.as_object_mut().ok_or("pipeline metadata is not an object")?.insert("requested_attachments".into(),serde_json::Value::Array(attachments));
  /* V7 is an exact captured class: retain the actual buffer slot/stride and
   * reject metadata changes instead of normalizing descriptor facts. */
  let vi=actual.get("vertex_input").ok_or("missing compositor vertex metadata")?;
  if vi.get("attributes").and_then(|v|v.as_array()).is_none_or(|a|a.len()!=2) ||
     vi.get("pulled").and_then(|v|v.as_array()).is_none_or(|a|!a.is_empty()) ||
     vi.get("bindings").and_then(|v|v.as_array()).is_none_or(|a|a.len()!=1) ||
     vi["bindings"][0]["buffer_index"].as_u64()!=Some(1) ||
     vi["bindings"][0]["stride"]["Static"].as_u64()!=Some(48) {
   return Err("8-tap compositor vertex descriptor outside exact slot1/stride48 class".into());
  }
  let mut expected:serde_json::Value=serde_json::from_str(include_str!("compositor_contract_v7.json")).map_err(|e|e.to_string())?;
  if source_over {
   let blends=blends;
   let exact=[RenderBlend{version:1,enabled:1,rgb_op:0,rgb_src:1,rgb_dst:5,alpha_op:0,alpha_src:1,alpha_dst:5,write_mask:15},
    RenderBlend{version:1,enabled:0,rgb_op:0,rgb_src:1,rgb_dst:0,alpha_op:0,alpha_src:1,alpha_dst:0,write_mask:0}];
   if blends.as_slice()!=exact{return Err("compositor blend descriptor outside exact premultiplied source-over class".into());}
   let ca=expected.get_mut("graphics").and_then(|v|v.get_mut("color_attachments")).and_then(|v|v.as_array_mut()).ok_or("missing expected color attachments")?;
   ca[0].as_object_mut().ok_or("bad expected target0")?.insert("blend".into(),serde_json::json!({"alpha_op":0,"color_op":0,"dst_alpha":7,"dst_color":7,"src_alpha":1,"src_color":1}));
   let requested:Vec<_>=blends.iter().map(|b|serde_json::json!({"version":b.version,"enabled":b.enabled,"rgb_op":b.rgb_op,"rgb_src":b.rgb_src,"rgb_dst":b.rgb_dst,"alpha_op":b.alpha_op,"alpha_src":b.alpha_src,"alpha_dst":b.alpha_dst,"write_mask":b.write_mask})).collect();
   expected.as_object_mut().ok_or("bad expected metadata")?.insert("requested_attachment_blends".into(),serde_json::Value::Array(requested.clone()));
   actual.as_object_mut().ok_or("bad actual metadata")?.insert("requested_attachment_blends".into(),serde_json::Value::Array(requested));
  } else {
   expected["graphics"]["color_attachments"][0]["blend"]=serde_json::Value::Null;
   expected.as_object_mut().ok_or("bad expected metadata")?.remove("requested_attachment_blends");
  }
  if actual!=expected {
   let mut mismatches=Vec::new();
   for key in ["color_format","validation","profile","stages","requirements","layout","interface","dependencies","vertex_input","graphics","variant_bits","requested_attachments"] {
    if actual.get(key)!=expected.get(key){mismatches.push(format!("{key}: {:?} != {:?}",actual.get(key),expected.get(key)));}
   }
   return Err(format!("translator compositor metadata differs from captured contract: {}",mismatches.join(",")));
  }
  Ok(())
 })} {Some(())=>0,None=>-1}
}

/// Exact metadata-only gate for the captured std_vert1_lph plus
/// home_affordance_base_lph pipeline. The second BGRA target is retained with
/// write mask zero, but this function does not approve native code or a draw.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_home_affordance(
 pipeline:*const RenderPipelineHandle,error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null home-affordance pipeline".into());}
  let handle=&*pipeline;let p=&handle._pipeline;
  let exact=[RenderColorAttachment{format:80,write_mask:15},RenderColorAttachment{format:80,write_mask:0}];
  if handle.requested_attachments.as_slice()!=exact||handle.requested_attachment_blends.is_some()||handle.source_blend.is_some(){
   return Err("home-affordance attachment descriptor changed".into());
  }
  let validation=handle._device.profile().spirv_validation();
  let stages:Vec<_>=p.stages.iter().map(|s|serde_json::json!({
   "stage":s.stage,"entry_point":s.entry_point,"specialization":s.specialization,
   "required_subgroup_size":s.required_subgroup_size,"stage_flags":s.stage_flags,
   "constant_blob":s.constant_blob.as_ref().map(|b|b.to_vec()),
   "rounded_colors":s.rounded_colors,"half_bit_colors":s.half_bit_colors,
   "precision_notes":s.precision_notes,"reconvergence":s.reconvergence
  })).collect();
  let actual=serde_json::json!({"schema":1,"scope":"render_compilation_not_native_draw_plan",
   "color_format":0,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},
   "profile":p.profile,"stages":stages,"requirements":{
    "api_version":p.requirements.api_version,"spirv_capabilities":p.requirements.spirv_capabilities,
    "spirv_extensions":p.requirements.spirv_extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "features":p.requirements.features,"extensions":p.requirements.extensions,
    "properties":p.requirements.properties.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "limits":p.requirements.limits.iter().map(|(k,v)|(format!("{k:?}"),*v)).collect::<std::collections::BTreeMap<_,_>>(),
    "formats":p.requirements.formats.iter().map(|(f,u)|(f,format!("{u:?}"))).collect::<Vec<_>>(),
    "stage_flags":p.requirements.stage_flags.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "denorm_independence":p.requirements.denorm_independence,"rounding_independence":p.requirements.rounding_independence},
   "layout":p.layout,"interface":p.interface,"dependencies":p.dependencies,
   "vertex_input":p.vertex_input,"graphics":p.graphics,"variant_bits":p.variant_bits,
   "requested_attachments":[{"format":80,"write_mask":15},{"format":80,"write_mask":0}]});
  let expected:serde_json::Value=serde_json::from_str(include_str!("home_affordance_contract.json")).map_err(|e|e.to_string())?;
  if actual!=expected {
   let mut mismatches=Vec::new();
   for key in ["color_format","validation","profile","stages","requirements","layout","interface","dependencies","vertex_input","graphics","variant_bits","requested_attachments"] {
    if actual.get(key)!=expected.get(key){mismatches.push(key);}
   }
   return Err(format!("translator home-affordance metadata differs from captured contract: {}",mismatches.join(",")));
  }
  Ok(())
 })} {Some(())=>0,None=>-1}
}

/// Exact metadata-only gate shared by the captured low-precision narrow-blur
/// 7/11/15/19/23/27 and single-pass-blur-3 pipelines. The caller separately
/// checks the exact function pair. This approves an observer object only;
/// native code generation and GPU submission remain separate gates.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_narrow_blur_11(
 pipeline:*const RenderPipelineHandle,error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null narrow-blur pipeline".into());}
  let handle=&*pipeline;let p=&handle._pipeline;
  let exact=[RenderColorAttachment{format:80,write_mask:15},RenderColorAttachment{format:80,write_mask:0}];
  if handle.requested_attachments.as_slice()!=exact||handle.requested_attachment_blends.is_some()||handle.source_blend.is_some(){
   return Err("narrow-blur attachment descriptor changed".into());
  }
  let validation=handle._device.profile().spirv_validation();
  let stages:Vec<_>=p.stages.iter().map(|s|serde_json::json!({
   "stage":s.stage,"entry_point":s.entry_point,"specialization":s.specialization,
   "required_subgroup_size":s.required_subgroup_size,"stage_flags":s.stage_flags,
   "constant_blob":s.constant_blob.as_ref().map(|b|b.to_vec()),
   "rounded_colors":s.rounded_colors,"half_bit_colors":s.half_bit_colors,
   "precision_notes":s.precision_notes,"reconvergence":s.reconvergence
  })).collect();
  let actual=serde_json::json!({"schema":1,"scope":"render_compilation_not_native_draw_plan",
   "color_format":0,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},
   "profile":p.profile,"stages":stages,"requirements":{
    "api_version":p.requirements.api_version,"spirv_capabilities":p.requirements.spirv_capabilities,
    "spirv_extensions":p.requirements.spirv_extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "features":p.requirements.features,"extensions":p.requirements.extensions,
    "properties":p.requirements.properties.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "limits":p.requirements.limits.iter().map(|(k,v)|(format!("{k:?}"),*v)).collect::<std::collections::BTreeMap<_,_>>(),
    "formats":p.requirements.formats.iter().map(|(f,u)|(f,format!("{u:?}"))).collect::<Vec<_>>(),
    "stage_flags":p.requirements.stage_flags.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "denorm_independence":p.requirements.denorm_independence,"rounding_independence":p.requirements.rounding_independence},
   "layout":p.layout,"interface":p.interface,"dependencies":p.dependencies,
   "vertex_input":p.vertex_input,"graphics":p.graphics,"variant_bits":p.variant_bits,
   "requested_attachments":[{"format":80,"write_mask":15},{"format":80,"write_mask":0}]});
  let expected:serde_json::Value=serde_json::from_str(include_str!("narrow_blur_11_contract.json")).map_err(|e|e.to_string())?;
  let expected7:serde_json::Value=serde_json::from_str(include_str!("narrow_blur_7_contract.json")).map_err(|e|e.to_string())?;
  let expected_single3:serde_json::Value=serde_json::from_str(include_str!("single_pass_blur_3_contract.json")).map_err(|e|e.to_string())?;
  if actual!=expected&&actual!=expected7&&actual!=expected_single3 {
   let mut mismatches=Vec::new();
   for key in ["color_format","validation","profile","stages","requirements","layout","interface","dependencies","vertex_input","graphics","variant_bits","requested_attachments"] {
    if actual.get(key)!=expected.get(key){mismatches.push(key);}
   }
   return Err(format!("translator narrow-blur metadata differs from captured family contracts: {}",mismatches.join(",")));
  }
  Ok(())
 })} {Some(())=>0,None=>-1}
}

/// Exact metadata half of the generated QuartzCore catalog gate. Function
/// names, complete Metal descriptor, donor identity and profile identity are
/// checked by the C frontend/catalog before it supplies this contract hash.
/// This gate never approves native code generation or execution.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_catalog_metadata_check(
 pipeline:*const RenderPipelineHandle,metadata_sha256:*const c_char,metadata_sha256_len:usize,
 error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null catalog pipeline".into());}
  if metadata_sha256.is_null()||metadata_sha256_len!=64{return Err("invalid catalog metadata identity".into());}
  let identity=std::str::from_utf8(std::slice::from_raw_parts(metadata_sha256.cast::<u8>(),metadata_sha256_len)).map_err(|_|"invalid catalog metadata identity")?;
  if !identity.bytes().all(|c|c.is_ascii_hexdigit()&&(!c.is_ascii_alphabetic()||c.is_ascii_lowercase())) {
   return Err("invalid catalog metadata identity".into());
  }
  let catalog:serde_json::Value=serde_json::from_str(include_str!("quartzcore_pipeline_catalog.json")).map_err(|e|e.to_string())?;
  let expected=catalog.get("contracts").and_then(|v|v.get(identity)).ok_or("metadata identity absent from catalog")?;
  let handle=&*pipeline;let p=&handle._pipeline;
  if handle.requested_attachment_blends.is_some()||handle.source_blend.is_some(){return Err("catalog pipeline has unexpected blend request".into());}
  let validation=handle._device.profile().spirv_validation();
  let stages:Vec<_>=p.stages.iter().map(|s|serde_json::json!({
   "stage":s.stage,"entry_point":s.entry_point,"specialization":s.specialization,
   "required_subgroup_size":s.required_subgroup_size,"stage_flags":s.stage_flags,
   "constant_blob":s.constant_blob.as_ref().map(|b|b.to_vec()),
   "rounded_colors":s.rounded_colors,"half_bit_colors":s.half_bit_colors,
   "precision_notes":s.precision_notes,"reconvergence":s.reconvergence
  })).collect();
  let requested:Vec<_>=handle.requested_attachments.iter().map(|a|serde_json::json!({"format":a.format,"write_mask":a.write_mask})).collect();
  let actual=serde_json::json!({"schema":1,"scope":"render_compilation_not_native_draw_plan",
   "color_format":0,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},
   "profile":p.profile,"stages":stages,"requirements":{
    "api_version":p.requirements.api_version,"spirv_capabilities":p.requirements.spirv_capabilities,
    "spirv_extensions":p.requirements.spirv_extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "features":p.requirements.features,"extensions":p.requirements.extensions,
    "properties":p.requirements.properties.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "limits":p.requirements.limits.iter().map(|(k,v)|(format!("{k:?}"),*v)).collect::<std::collections::BTreeMap<_,_>>(),
    "formats":p.requirements.formats.iter().map(|(f,u)|(f,format!("{u:?}"))).collect::<Vec<_>>(),
    "stage_flags":p.requirements.stage_flags.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),
    "denorm_independence":p.requirements.denorm_independence,"rounding_independence":p.requirements.rounding_independence},
   "layout":p.layout,"interface":p.interface,"dependencies":p.dependencies,
   "vertex_input":p.vertex_input,"graphics":p.graphics,"variant_bits":p.variant_bits,
   "requested_attachments":requested});
  if &actual!=expected {
   let mut mismatches=Vec::new();
   for key in ["color_format","validation","profile","stages","requirements","layout","interface","dependencies","vertex_input","graphics","variant_bits","requested_attachments"] {
    if actual.get(key)!=expected.get(key){mismatches.push(key);}
   }
   return Err(format!("translator metadata differs from catalog contract {identity}: {}",mismatches.join(",")));
  }
  Ok(())
 })} {Some(())=>0,None=>-1}
}

/// Native fixed-function fetch contract for kernels exposing the vertex-draw
/// capability. Resource, varying, blend and shader requirements remain checked.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_with_vertex_fetch(
 pipeline:*const RenderPipelineHandle,format:u32,error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  if (*pipeline).source_blend.is_some_and(|b|b.enabled!=0){return Err("native render blend requires explicit gate".into());}
  native_render_check(&(*pipeline)._pipeline,format,true,None)
 })} {Some(())=>0,None=>-1}
}

/// Exact R80 BackBoard fixed-UI resource gate. This admits only the compiled
/// pair with vertex buffer2/[0,64), fragment buffer1/[0,168), the reflected
/// binding-offset push fields at 0/64/68, and one eight-byte stage constant
/// blob. Dynamic buffer ownership and draw-range checks remain in the typed
/// draw planner and native resource mapper.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_fixed_ui_r80(
 pipeline:*const RenderPipelineHandle,format:u32,error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  if format!=80{return Err("R80 fixed-UI gate requires BGRA8Unorm target".into());}
  let handle=&*pipeline;
  if handle.source_blend.is_some_and(|b|b.enabled!=0){return Err("R80 fixed-UI gate refuses enabled blend".into());}
  native_render_check_policy(&handle._pipeline,format,true,None,NativeResources::FixedUiR80Buffers)
 })} {Some(())=>0,None=>-1}
}

/// Exact sampled BackBoard fixed-UI gate.  In addition to the four bounded
/// uniform windows it admits one normalized runtime FS texture3/sampler0 pair,
/// the fragment constant blob, and the sampler specialization fixed at zero.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_fixed_ui_r85_texture(
 pipeline:*const RenderPipelineHandle,format:u32,error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  if format!=80{return Err("R85 textured fixed-UI gate requires BGRA8Unorm target".into());}
  let handle=&*pipeline;
  if handle.source_blend.is_some_and(|b|b.enabled!=0){return Err("R85 textured fixed-UI gate refuses enabled blend".into());}
  native_render_check_policy(&handle._pipeline,format,true,None,NativeResources::FixedUiR85Texture)
 })} {Some(())=>0,None=>-1}
}

/// Metal slots of the one admitted texture binding, NOT Mali descriptor
/// indices/addresses. Binding must later enforce a normalized, nearest,
/// clamp, WeightedAverage, non-anisotropic, non-compare sampler.
#[repr(C)]
#[derive(Clone,Copy,PartialEq,Eq,Debug)]
pub struct TexturePlan {pub version:u32,pub stage:u32,pub texture_index:u32,pub sampler_index:u32,
 pub descriptor_set:u32,pub binding:u32,pub normalized_coordinates:u32}
/// Additive bounded texture gate: RGBA8Unorm/BGRA8Unorm targets (70/80), unblended, qualified static
/// fetch, and exactly the FS texture0/sampler0 contract. Old gates still
/// refuse every resource. Output is written only on success.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_with_texture(
 pipeline:*const RenderPipelineHandle,format:u32,out:*mut TexturePlan,
 error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe {error_copy(error,cap,"null texture plan output")};return -1;}
 // Never write an error over the plan (or vice versa).
 let (o,e)=(out as usize,error as usize);
 if !error.is_null()&&cap>0&&o<e.saturating_add(cap)&&e<o+size_of::<TexturePlan>() {return -1;}
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  if format!=70&&format!=80 {return Err("native texture gate supports RGBA8Unorm/BGRA8Unorm targets only".into());}
  let handle=&*pipeline;
  // Even an optimized-away One/Zero replacement must use the blend gate.
  if handle.source_blend.is_some_and(|b|b.enabled!=0){return Err("native texture gate refuses enabled blend".into());}
  let p=&handle._pipeline;
  native_render_check_policy(p,format,true,None,NativeResources::FragmentTexture0Sampler0)?;
  render_push(&p.interface.push,&p.graphics.color_write_rules)?;
  // Derived from the verified binding, not restated constants.
  let b=&p.layout.sets[0].bindings[0];
  let metal2vulkan::pipeline::BindingSource::CombinedTextureSampler{texture,sampler}=b.source else {
   return Err("native texture binding source changed".into());};
  let metal2vulkan::pipeline::SamplerRef::Slot(sampler_index)=sampler.sampler else {
   return Err("native texture sampler is not a runtime slot".into());};
  Ok(TexturePlan{version:1,stage:b.stages.as_raw(),texture_index:texture.index,sampler_index,
   descriptor_set:p.layout.sets[0].set,binding:b.binding,normalized_coordinates:1})
 })} {Some(plan)=>{unsafe {*out=plan};0},None=>-1}
}

fn compositor_plan_inputs(resources:&[RenderResource])->Result<(metal2vulkan::bind::BindState,serde_json::Value),String>{
 compositor_plan_inputs_mode(resources,false)
}

/* `allow_exact_sampler` admits every validated sampler descriptor the
 * translator models, preserving its real filter/mip/address/compare/LOD state.
 * The strict mode remains the fixed-UI/blur legacy admission: those transports
 * have no exact sampler word and must not silently receive a different state. */
fn compositor_plan_inputs_mode(resources:&[RenderResource],allow_exact_sampler:bool)->Result<(metal2vulkan::bind::BindState,serde_json::Value),String>{
 compositor_plan_inputs_impl(resources,allow_exact_sampler,true)
}
/* The same BindState without the JSON summary (per-draw planners discard it;
 * building and dropping it was a large share of their CPU). */
fn compositor_plan_state(resources:&[RenderResource],allow_exact_sampler:bool)->Result<metal2vulkan::bind::BindState,String>{
 compositor_plan_inputs_impl(resources,allow_exact_sampler,false).map(|(state,_)|state)
}
fn compositor_plan_inputs_impl(resources:&[RenderResource],allow_exact_sampler:bool,summarize:bool)->Result<(metal2vulkan::bind::BindState,serde_json::Value),String>{
 use std::collections::{BTreeMap,BTreeSet};
 use metal2vulkan::{bind::{BindState,BoundBuffer,BoundSampler,BoundTexture,SamplerDescriptor,StageTable,TextureFacts},
  metal::{self,PixelFormat,TextureType,TextureSwizzle,TextureSwizzleChannels},pipeline::Stage};
 if resources.len()>186{return Err("compositor resource count exceeds two-stage typed slot bound".into());}
 let mut keys=BTreeSet::new();let mut vs=StageTable::default();let mut fs=StageTable::default();
 let mut summaries=Vec::new();
 for r in resources {
  if !keys.insert((r.kind,r.stage,r.index)){return Err("duplicate compositor resource stage/kind/index".into());}
  let stage=match r.stage {1=>Stage::Vertex,2=>Stage::Fragment,_=>return Err("unsupported compositor resource stage".into())};
  if r.index>=31{return Err("compositor resource index exceeds typed slot bound".into());}
  match r.kind {
   1=>{
    if r.format!=0||r.texture_type!=0||r.width!=0||r.height!=0||r.array_length!=0||r.mipmap_levels!=0||r.sample_count!=0||r.usage!=0||r.sparse!=0||r.depth_twin!=0||r.swizzle!=[0;4]||r.parent_level!=0||r.parent_width!=0||r.parent_height!=0||r.parent_depth!=0||r.own_format_views!=0||r.address_mode_identity!=0||r.gpu_address!=0||r.resource_id!=0||r.stride!=0||r.reserved!=0||r.min_filter!=0||r.mag_filter!=0||r.mip_filter!=0||r.address_s!=0||r.address_t!=0||r.address_r!=0||r.border_color!=0||r.normalized_coordinates!=0||r.lod_average!=0||r.max_anisotropy!=0||r.compare_function!=0||r.reduction_mode!=0||r.lod_min_clamp!=0.0||r.lod_max_clamp!=0.0{return Err("buffer resource has irrelevant or unsupported fields; GPU addresses are not accepted".into());}
    if r.length==0||r.length>(1u64<<32)||r.offset>r.length{return Err("compositor buffer length/offset is invalid".into());}
    // The caller address is only an overlap hint. Do not copy an untrusted
    // GPU VA into the translator's binding state or emitted draw commands.
    let b=BoundBuffer::Buffer{length:r.length,offset:r.offset,gpu_address:0,device_address:None,attribute_stride:None};
    if stage==Stage::Vertex{vs.buffers.insert(r.index,b);}else{fs.buffers.insert(r.index,b);}
    if summarize{summaries.push(serde_json::json!({"kind":"buffer","stage":r.stage,"index":r.index,"length":r.length,"offset":r.offset,"available":r.length-r.offset,"gpu_address_supplied":false,"ownership_proven":false}));}
   },
   2=>{
    // The narrow-blur observer samples the exact private BGRA mip chain seen
    // on Tegu.  The sphere observer samples the exact R8 64x64 Type3D class
    // whose constexpr sampler is carried by the pipeline layout, so there is
    // deliberately no caller-owned sampler resource for that class.  Both
    // remain metadata-only here; no native submission gate consumes this plan.
    let observed_blur_mips=r.format==80&&r.width==776&&r.height==24&&r.mipmap_levels==10&&r.usage==5;
    let observed_sphere_volume=r.format==10&&r.texture_type==7&&r.width==64&&r.height==64&&r.mipmap_levels==1&&r.usage==1;
    let mip_range=r.mipmap_levels>=1&&r.mipmap_levels<=16;
    // 43 = BGR5A1Unorm: translator VK A1R5G5B5_UNORM_PACK16 (B in bits 0-4,
    // A bit 15), Mali RGB5_A1_UNORM with BGRA order (n2t, gd_format) - the
    // same channel order as the delta19 render-target class (Mesa
    // BFMT(B5G5R5A1_UNORM,R5G5B5A1)). QuartzCore samples its blur pyramid.
    // Depth16Unorm/Depth32Float: depth2d sample and sample_compare (n2t Z16 /
    // Z32F descriptors, compare samplers) on the specialized path.
    let sampled_format=matches!(r.format,1|10|30|43|70|80|115)||
     (allow_exact_sampler&&(n2t_sampled_format(r.format)||matches!(r.format,250|252)));
    // New-only bounded sampled 2D-array/mip class. Keep the exact resource
    // facts; the selected view's format/level/type is filled by the sidecar.
    let rich_mip_2d=allow_exact_sampler&&sampled_format&&r.texture_type==2&&r.array_length==1&&
     mip_range&&r.sample_count==1&&(r.usage==1||r.usage==5);
    let rich_array=allow_exact_sampler&&sampled_format&&r.texture_type==3&&r.array_length>=1&&r.array_length<=256&&
     mip_range&&(u64::from(r.array_length)*u64::from(r.mipmap_levels)<=256)&&
     r.sample_count==1&&(r.usage==1||r.usage==5);
    // ASTC remains new-only and single-level in this bounded class.
    let rich_astc=allow_exact_sampler&&generic_astc_2d_format(r.format)&&r.texture_type==2&&
     r.array_length==1&&r.mipmap_levels==1&&r.sample_count==1&&(r.usage==1||r.usage==5);
    // Existing single-level 2D classes, including the observed blur chain.
    let bounded_2d=matches!(r.format,1|10|30|43|70|80|115)&&r.texture_type==2&&
     (observed_blur_mips||(mip_range&&r.mipmap_levels==1&&(r.usage==1||r.usage==5)));
    // Specialized path: the remaining Metal texture types the frontend
    // allocates and n2t packs (1D/1DArray one row a level, cube faces as
    // layers, slice-major 3D, sample-major MS), bounded like rich_array.
    let ms_samples=matches!(r.sample_count,2|4|8);
    let usage_ok=r.usage==1||r.usage==5;
    let rich_types=allow_exact_sampler&&sampled_format&&usage_ok&&mip_range&&match r.texture_type {
     0=>r.height==1&&r.array_length==1&&r.sample_count==1,
     1=>r.height==1&&(1..=256).contains(&r.array_length)&&r.sample_count==1&&
        u64::from(r.array_length)*u64::from(r.mipmap_levels)<=256,
     5=>r.width==r.height&&r.array_length==1&&r.sample_count==1&&6*r.mipmap_levels<=256,
     6=>r.width==r.height&&(1..=42).contains(&r.array_length)&&r.sample_count==1&&
        6*u64::from(r.array_length)*u64::from(r.mipmap_levels)<=256,
     7=>r.array_length==1&&r.sample_count==1,
     4=>r.array_length==1&&r.mipmap_levels==1&&ms_samples,
     8=>(1..=256).contains(&r.array_length)&&r.mipmap_levels==1&&ms_samples,
     _=>false};
    let multi_layer=rich_array||(rich_types&&matches!(r.texture_type,1|6|8));
    let bounded=observed_sphere_volume||bounded_2d||rich_mip_2d||rich_array||rich_astc||rich_types;
    if r.format==0||!bounded||r.width==0||r.height==0||r.width>16384||r.height>16384||
       (!multi_layer&&r.array_length!=1)||(r.sample_count!=1&&!(rich_types&&ms_samples))||r.sparse!=0||r.depth_twin!=0||
       r.parent_level!=0||r.parent_width!=0||r.parent_height!=0||r.parent_depth!=0||
       r.own_format_views!=0||r.address_mode_identity!=1||r.length!=0||r.offset!=0||
       r.gpu_address!=0||r.stride!=0||r.reserved!=0||r.min_filter!=0||r.mag_filter!=0||
       r.mip_filter!=0||r.address_s!=0||r.address_t!=0||r.address_r!=0||r.border_color!=0||
       r.normalized_coordinates!=0||r.lod_average!=0||r.max_anisotropy!=0||r.compare_function!=0||
       r.reduction_mode!=0||r.lod_min_clamp!=0.0||r.lod_max_clamp!=0.0{
      return Err("texture facts outside bounded read-only 2D/array/sphere-volume classes".into());
    }
    let swizzle=TextureSwizzleChannels::new(TextureSwizzle::try_from(r.swizzle[0]).map_err(|e|e.to_string())?,TextureSwizzle::try_from(r.swizzle[1]).map_err(|e|e.to_string())?,TextureSwizzle::try_from(r.swizzle[2]).map_err(|e|e.to_string())?,TextureSwizzle::try_from(r.swizzle[3]).map_err(|e|e.to_string())?);
    if swizzle!=TextureSwizzleChannels::DEFAULT||r.resource_id==0{return Err("texture swizzle/resource id outside bounded plan".into());}
    let mut facts=TextureFacts::new(TextureType::try_from(r.texture_type).map_err(|e|e.to_string())?,PixelFormat::try_from(r.format).map_err(|e|e.to_string())?);
    facts.sample_count=r.sample_count;facts.usage=metal::TextureUsage::try_from(r.usage).map_err(|e|e.to_string())?;facts.array_length=r.array_length;facts.mipmap_level_count=r.mipmap_levels;facts.swizzle=swizzle;
    if !facts.usage.contains(metal::TextureUsage::SHADER_READ){return Err("texture must include shader-read usage".into());}
    if stage==Stage::Vertex{vs.textures.insert(r.index,BoundTexture::Texture{facts,resource_id:Some(r.resource_id)});}else{fs.textures.insert(r.index,BoundTexture::Texture{facts,resource_id:Some(r.resource_id)});}
    if summarize{summaries.push(serde_json::json!({"kind":"texture","stage":r.stage,"index":r.index,"format":r.format,"texture_type":r.texture_type,"width":r.width,"height":r.height,"mip_levels":r.mipmap_levels,"sample_count":r.sample_count,"usage":r.usage,"swizzle":"identity","address_modes":"identity","resource_token_only_not_owned":true,"storage_layout":"opaque/unknown","row_stride_bytes":null,"backing_span_bytes":null}));}
   },
   3=>{
    // The pinned translator models Metal's `samplers[16]` table. Keep the
    // input wire range explicit and refuse out-of-table slots here.
    if r.index>=16{return Err("sampler index exceeds pinned translator bound 0..15".into());}
    if r.format!=0||r.texture_type!=0||r.width!=0||r.height!=0||r.array_length!=0||r.mipmap_levels!=0||r.sample_count!=0||r.usage!=0||r.sparse!=0||r.depth_twin!=0||r.swizzle!=[0;4]||r.parent_level!=0||r.parent_width!=0||r.parent_height!=0||r.parent_depth!=0||r.own_format_views!=0||r.address_mode_identity!=0||r.length!=0||r.offset!=0||r.gpu_address!=0||r.resource_id!=0||r.stride!=0||r.reserved!=0||r.normalized_coordinates>1{return Err("sampler resource has irrelevant or unsupported fields".into());}
    let mut desc=SamplerDescriptor::new();desc.min_filter=metal::SamplerMinMagFilter::try_from(r.min_filter).map_err(|e|e.to_string())?;desc.mag_filter=metal::SamplerMinMagFilter::try_from(r.mag_filter).map_err(|e|e.to_string())?;desc.mip_filter=metal::SamplerMipFilter::try_from(r.mip_filter).map_err(|e|e.to_string())?;desc.s_address_mode=metal::SamplerAddressMode::try_from(r.address_s).map_err(|e|e.to_string())?;desc.t_address_mode=metal::SamplerAddressMode::try_from(r.address_t).map_err(|e|e.to_string())?;desc.r_address_mode=metal::SamplerAddressMode::try_from(r.address_r).map_err(|e|e.to_string())?;desc.border_color=metal::SamplerBorderColor::try_from(r.border_color).map_err(|e|e.to_string())?;desc.normalized_coordinates=r.normalized_coordinates==1;desc.lod_min_clamp=r.lod_min_clamp;desc.lod_max_clamp=r.lod_max_clamp;desc.lod_average=r.lod_average!=0;desc.max_anisotropy=r.max_anisotropy;desc.compare_function=metal::CompareFunction::try_from(r.compare_function).map_err(|e|e.to_string())?;desc.reduction_mode=metal::SamplerReductionMode::try_from(r.reduction_mode).map_err(|e|e.to_string())?;
    if allow_exact_sampler {
     if r.lod_average>1||!(1..=16).contains(&r.max_anisotropy)||
        !r.lod_min_clamp.is_finite()||!r.lod_max_clamp.is_finite()||
        (r.lod_max_clamp!=0.0&&r.lod_min_clamp>r.lod_max_clamp) {
      return Err("sampler exact state is outside the translator domain".into());
     }
    } else {
     let mut expected=SamplerDescriptor::new();expected.lod_max_clamp=0.0;if desc!=expected{return Err("sampler must match nearest/normalized/clamp/LOD0/no-compare".into());}
    }
    if stage==Stage::Vertex{vs.samplers.insert(r.index,BoundSampler::new(desc));}else{fs.samplers.insert(r.index,BoundSampler::new(desc));}
    if summarize{summaries.push(serde_json::json!({"kind":"sampler","stage":r.stage,"index":r.index,"descriptor":if allow_exact_sampler{"exact runtime sampler state"}else{"nearest normalized clamp-to-edge lod0; no compare/anisotropy"}}));}
   },
   _=>return Err("unknown compositor resource kind".into()),
  }
 }
 let mut state=BindState::default();state.stages=BTreeMap::from([(Stage::Vertex,vs),(Stage::Fragment,fs)]);
 Ok((state,serde_json::Value::Array(summaries)))
}

fn compositor_ranges_overlap(a:usize,an:usize,b:usize,bn:usize)->bool{
 if an==0||bn==0{return false;}let ae=a.checked_add(an);let be=b.checked_add(bn);match(ae,be){(Some(ae),Some(be))=>a<be&&b<ae,_=>true}
}

/* Typed snapshot contract. `range_offset/range_length` are copied from the
 * translator's ResourceUse::range (relative to the Metal binding offset).
 * flags: bit0=read, bit1=write, bit2=bounded range, bit3=sampler coordinates,
 * bit4=sampler reduction, bit5=whole-range use (dominates bounded uses).
 * class_mask bitN means AccessClass code N+1 occurs for this slot. */
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct RenderResourceUse {pub kind:u32,pub stage:u32,pub index:u32,pub class_mask:u32,
 pub flags:u32,pub reserved:u32,pub range_offset:u64,pub range_length:u64}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct RenderTypedPlan {pub version:u32,pub count:u32,pub attachment_count:u32,
 pub diagnostic_count:u32,pub neutral_count:u32,pub reserved:[u32;3],
 pub attachments:[RenderColorAttachment;8],pub uses:[RenderResourceUse;186]}
impl Default for RenderTypedPlan {fn default()->Self{Self{version:0,count:0,attachment_count:0,
 diagnostic_count:0,neutral_count:0,reserved:[0;3],attachments:[RenderColorAttachment{format:0,write_mask:0};8],
 uses:[RenderResourceUse::default();186]}}}

/* Bounded execution-facing projection for the exact R80 fixed UI pipeline.
 * Descriptor ranges are allocation-relative; push spans retain their absolute
 * Vulkan offsets while bytes_offset addresses this plan's compact push_bytes. */
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct FixedUiR80Binding {pub stage:u32,pub index:u32,pub descriptor_set:u32,pub binding:u32,
 pub bind_offset:u64,pub resource_length:u64,
 pub descriptor_offset:u64,pub descriptor_length:u64}
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct FixedUiR80PushSpan {pub stage:u32,pub offset:u32,pub length:u32,pub bytes_offset:u32}
#[repr(C)]
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub struct FixedUiR80Plan {pub version:u32,pub binding_count:u32,pub push_count:u32,pub push_bytes_length:u32,
 pub constant_stage:u32,pub constant_length:u32,pub reserved:[u32;2],
 pub bindings:[FixedUiR80Binding;2],pub pushes:[FixedUiR80PushSpan;2],
 pub push_bytes:[u8;12],pub constant_bytes:[u8;8]}
impl Default for FixedUiR80Plan {fn default()->Self{Self{version:0,binding_count:0,push_count:0,
 push_bytes_length:0,constant_stage:0,constant_length:0,reserved:[0;2],
 bindings:[FixedUiR80Binding::default();2],pushes:[FixedUiR80PushSpan::default();2],
 push_bytes:[0;12],constant_bytes:[0;8]}}}

/* Complete execution projection for the exact R85 sampled fixed-UI class.
 * Buffer entries cover descriptor bindings 0..3.  The texture entry names the
 * combined binding and the retained texture token; the sampler is a separately
 * retained Metal slot.  Specialization bytes are the Vulkan constant key the
 * admitted pipeline was planned with. */
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct FixedUiR85Binding {pub stage:u32,pub index:u32,pub descriptor_set:u32,pub binding:u32,
 pub bind_offset:u64,pub resource_length:u64,pub descriptor_offset:u64,pub descriptor_length:u64}
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct FixedUiR85PushSpan {pub stage:u32,pub offset:u32,pub length:u32,pub bytes_offset:u32}
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct FixedUiR85TextureBinding {pub texture_stage:u32,pub texture_index:u32,
 pub sampler_stage:u32,pub sampler_index:u32,pub descriptor_set:u32,pub binding:u32,
 pub format:u32,pub texture_type:u32,pub width:u32,pub height:u32,pub resource_id:u64}
#[repr(C)]
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub struct FixedUiR85Plan {pub version:u32,pub binding_count:u32,pub push_count:u32,pub push_bytes_length:u32,
 pub constant_stage:u32,pub constant_length:u32,pub specialization_id:u32,pub specialization_length:u32,
 pub bindings:[FixedUiR85Binding;4],pub texture:FixedUiR85TextureBinding,
 pub pushes:[FixedUiR85PushSpan;2],pub push_bytes:[u8;20],pub constant_bytes:[u8;8],
 pub specialization_bytes:[u8;4]}
impl Default for FixedUiR85Plan {fn default()->Self{Self{version:0,binding_count:0,push_count:0,
 push_bytes_length:0,constant_stage:0,constant_length:0,specialization_id:0,specialization_length:0,
 bindings:[FixedUiR85Binding::default();4],texture:FixedUiR85TextureBinding::default(),
 pushes:[FixedUiR85PushSpan::default();2],push_bytes:[0;20],constant_bytes:[0;8],specialization_bytes:[0;4]}}}

fn fixed_ui_r80_pack_pushes<'a,I>(writes:I,out:&mut FixedUiR80Plan)->Result<(),String>
where I:IntoIterator<Item=(u32,u32,&'a [u8])> {
 let mut values:Vec<_>=writes.into_iter().collect();
 values.sort_by_key(|(stage,offset,_)|(*stage,*offset));
 if values.len()!=2 || values[0].0!=1 || values[0].1!=0 || values[0].2.len()!=4 ||
    values[1].0!=16 || values[1].1!=64 || values[1].2.len()!=8 {
  return Err("R80 fixed-UI draw has unexpected push writes".into());
 }
 let mut cursor=0usize;
 for (i,(stage,offset,bytes)) in values.into_iter().enumerate() {
  let end=cursor+bytes.len();out.push_bytes[cursor..end].copy_from_slice(bytes);
  out.pushes[i]=FixedUiR80PushSpan{stage,offset,length:bytes.len() as u32,bytes_offset:cursor as u32};cursor=end;
 }
 out.push_count=2;out.push_bytes_length=cursor as u32;Ok(())
}

fn fixed_ui_r80_input_resources(resources:&[RenderResource])->Result<[&RenderResource;2],String>{
 if resources.len()!=2{return Err("R80 fixed-UI draw requires exactly two resources".into());}
 let find=|stage,index|resources.iter().find(|r|r.kind==1&&r.stage==stage&&r.index==index)
  .ok_or_else(||"R80 fixed-UI draw is missing an exact buffer slot".to_owned());
 let vertex=find(1,2)?;let fragment=find(2,1)?;
 for (resource,needed) in [(vertex,64u64),(fragment,168u64)] {
  if resource.length==0||resource.offset.checked_add(needed).is_none_or(|end|end>resource.length) {
   return Err("R80 fixed-UI buffer window exceeds its retained allocation".into());
  }
 }
 Ok([vertex,fragment])
}

fn fixed_ui_r80_plan(handle:&RenderPipelineHandle,resources:&[RenderResource],d:&RenderDraw)->Result<FixedUiR80Plan,String>{
 use metal2vulkan::{bind::{BoundBuffer,WriteSource},pipeline::{SlotClass,Stage}};
 if handle.requested_attachments.as_slice()!=[RenderColorAttachment{format:80,write_mask:15}] ||
    handle.source_blend.is_some_and(|b|b.enabled!=0) {
  return Err("R80 fixed-UI plan requires one unblended BGRA8 target".into());
 }
 native_render_check_policy(&handle._pipeline,80,true,None,NativeResources::FixedUiR80Buffers)?;
 if d.version!=1||d.vertex_count==0||d.vertex_count>(1<<24)||d.instance_count==0||d.instance_count>(1<<20)||
    d.vertex_start.checked_add(d.vertex_count).is_none()||d.base_instance.checked_add(d.instance_count).is_none(){
  return Err("invalid or unbounded R80 fixed-UI direct draw range".into());
 }
 let exact=fixed_ui_r80_input_resources(resources)?;
 let mut state=compositor_plan_state(resources,false)?;
 /* Slot 1 is fixed-function vertex fetch, not a descriptor resource.  Give the
  * translator a bounded shape for planning; the frontend separately retains
  * and range-checks the real allocation before producing a kernel request. */
 let vertex_end=d.vertex_start.checked_add(d.vertex_count).ok_or("fixed UI vertex range overflow")? as u64;
 let vertex_bytes=vertex_end.checked_mul(48).ok_or("fixed UI vertex byte range overflow")?;
 let vertex_stage=state.stages.get_mut(&Stage::Vertex).ok_or("fixed UI vertex stage missing")?;
 vertex_stage.buffers.insert(1,BoundBuffer::Buffer{length:vertex_bytes,offset:0,gpu_address:0,
  device_address:None,attribute_stride:Some(48)});
 let plan_ref=handle.cached_plan(&state)?;let plan=&*plan_ref;
 if !plan.variant.is_empty()||!plan.specialization.data.is_empty()||!plan.specialization.entries.is_empty()||
    !plan.diagnostics.is_empty()||plan.descriptors.len()!=3 {
  return Err("R80 fixed-UI binding plan has variants, diagnostics, or extra descriptors".into());
 }
 for (binding,stage,index) in [(0u32,Stage::Vertex,2u32),(1,Stage::Fragment,1)] {
  let Some(write)=plan.descriptors.iter().find(|w|w.set==0&&w.binding==binding) else {
   return Err("R80 fixed-UI binding plan is missing an exact descriptor".into());};
  if write.array_element!=0||write.ty.as_raw()!=6||
     !matches!(write.source,WriteSource::Buffer{slot} if slot.stage==stage&&slot.class==SlotClass::Buffer&&slot.index==index) {
   return Err("R80 fixed-UI descriptor source changed".into());
  }
 }
 let Some(constant)=plan.descriptors.iter().find(|w|w.set==0&&w.binding==2) else {
  return Err("R80 fixed-UI binding plan is missing its constant descriptor".into());};
 if constant.array_element!=0||constant.ty.as_raw()!=7||
    !matches!(constant.source,WriteSource::ConstantBlob{stage:Stage::Fragment}) {
  return Err("R80 fixed-UI constant descriptor source changed".into());
 }
 let draw=metal2vulkan::bind::DrawCall::Primitives{vertex_start:d.vertex_start,vertex_count:d.vertex_count,
  instance_count:d.instance_count,base_instance:d.base_instance};
 let commands=plan.instantiate_draw(&state,&draw).map_err(|e|e.to_string())?;
 if !commands.descriptor_bytes.is_empty()||!commands.push_holes.is_empty()||!commands.root_tables.is_empty()||
    !commands.vertex_strides.is_empty()||!commands.uses.is_empty()||commands.indirect_needs_arguments||
    commands.index_overrun.is_some()||commands.index_bounds_pass||!commands.amplifications.is_empty()||
    commands.specialization.is_some()||commands.mesh_dispatch.is_some()||commands.descriptor_ranges.len()!=2 {
  return Err("R80 fixed-UI draw requires unsupported auxiliary state".into());
 }
 match commands.draw {vk::DrawCommand::Draw{vertex_count,instance_count,first_vertex,first_instance}
   if vertex_count==d.vertex_count&&instance_count==d.instance_count&&first_vertex==d.vertex_start&&first_instance==d.base_instance=>{},
  _=>return Err("R80 fixed-UI draw command changed form".into())}
 let mut out=FixedUiR80Plan::default();out.version=1;out.binding_count=2;
 for (i,(resource,set,binding)) in [(exact[0],0u32,0u32),(exact[1],0,1)].into_iter().enumerate() {
  let range=commands.descriptor_ranges.iter().find(|r|r.set==set&&r.binding==binding)
   .ok_or("R80 fixed-UI descriptor range is missing")?;
  out.bindings[i]=FixedUiR80Binding{stage:resource.stage,index:resource.index,descriptor_set:set,binding,
   bind_offset:resource.offset,resource_length:resource.length,
   descriptor_offset:range.range.offset,descriptor_length:range.range.range};
 }
 fixed_ui_r80_pack_pushes(commands.push_constants.iter().map(|p|(p.stages.as_raw(),p.offset,p.bytes.as_slice())),&mut out)?;
 let mut blobs=handle._pipeline.stages.iter().filter_map(|s|s.constant_blob.as_ref().map(|b|(s.stage.as_raw(),b)));
 let (stage,blob)=blobs.next().ok_or("R80 fixed-UI constant blob is missing")?;
 if blobs.next().is_some()||blob.len()!=8||!(stage==1||stage==16){return Err("R80 fixed-UI constant blob changed".into());}
 out.constant_stage=stage;out.constant_length=8;out.constant_bytes.copy_from_slice(blob);Ok(out)
}

fn fixed_ui_r85_pack_pushes<'a,I>(writes:I,out:&mut FixedUiR85Plan)->Result<(),String>
where I:IntoIterator<Item=(u32,u32,&'a [u8])> {
 let mut values:Vec<_>=writes.into_iter().collect();values.sort_by_key(|(stage,offset,_)|(*stage,*offset));
 if values.len()!=2||values[0].0!=1||values[0].1!=0||values[0].2.len()!=8||
    values[1].0!=16||values[1].1!=64||values[1].2.len()!=12 {
  return Err("R85 textured fixed-UI draw has unexpected push writes".into());
 }
 let mut cursor=0usize;
 for (i,(stage,offset,bytes)) in values.into_iter().enumerate(){let end=cursor+bytes.len();
  out.push_bytes[cursor..end].copy_from_slice(bytes);
  out.pushes[i]=FixedUiR85PushSpan{stage,offset,length:bytes.len() as u32,bytes_offset:cursor as u32};cursor=end;}
 out.push_count=2;out.push_bytes_length=cursor as u32;Ok(())
}

fn fixed_ui_r85_input_resources(resources:&[RenderResource])->Result<[&RenderResource;7],String>{
 if resources.len()!=7{return Err("R85 textured fixed-UI draw requires exactly seven resources".into());}
 let find=|kind,stage,index|resources.iter().find(|r|r.kind==kind&&r.stage==stage&&r.index==index)
  .ok_or_else(||"R85 textured fixed-UI draw is missing an exact resource slot".to_owned());
 let exact=[find(1,1,2)?,find(1,1,3)?,find(1,2,1)?,find(1,2,2)?,find(1,1,1)?,find(2,2,3)?,find(3,2,0)?];
 for (resource,needed) in exact[..4].iter().zip([64u64,16,168,8]) {
  if resource.length==0||resource.offset.checked_add(needed).is_none_or(|end|end>resource.length){
   return Err("R85 textured fixed-UI buffer window exceeds its retained allocation".into());}
 }
 Ok(exact)
}

fn fixed_ui_r85_plan(handle:&RenderPipelineHandle,resources:&[RenderResource],d:&RenderDraw)->Result<FixedUiR85Plan,String>{
 use metal2vulkan::{bind::{BoundBuffer,ImageSource,SamplerSource,WriteSource},pipeline::{SlotClass,Stage}};
 if handle.requested_attachments.as_slice()!=[RenderColorAttachment{format:80,write_mask:15}]||
    handle.source_blend.is_some_and(|b|b.enabled!=0){return Err("R85 textured fixed-UI plan requires one unblended BGRA8 target".into());}
 native_render_check_policy(&handle._pipeline,80,true,None,NativeResources::FixedUiR85Texture)?;
 if d.version!=1||d.vertex_count==0||d.vertex_count>(1<<24)||d.instance_count==0||d.instance_count>(1<<20)||
    d.vertex_start.checked_add(d.vertex_count).is_none()||d.base_instance.checked_add(d.instance_count).is_none(){
  return Err("invalid or unbounded R85 textured fixed-UI direct draw range".into());}
 let exact=fixed_ui_r85_input_resources(resources)?;let mut state=compositor_plan_state(resources,false)?;
 let vertex_end=d.vertex_start.checked_add(d.vertex_count).ok_or("textured fixed UI vertex range overflow")? as u64;
 let vertex_bytes=vertex_end.checked_mul(48).ok_or("textured fixed UI vertex byte range overflow")?;
 if exact[4].offset.checked_add(vertex_bytes).is_none_or(|end|end>exact[4].length){
  return Err("R85 textured fixed-UI vertex fetch exceeds its retained allocation".into());}
 let vertex_stage=state.stages.get_mut(&Stage::Vertex).ok_or("textured fixed UI vertex stage missing")?;
 vertex_stage.buffers.insert(1,BoundBuffer::Buffer{length:vertex_bytes,offset:0,gpu_address:0,
  device_address:None,attribute_stride:Some(48)});
 let plan_ref=handle.cached_plan(&state)?;let plan=&*plan_ref;
 if !plan.variant.is_empty()||plan.diagnostics.len()!=0||plan.descriptors.len()!=6||
    plan.specialization.data!=[0,0,0,0]||plan.specialization.entries.len()!=1||
    plan.specialization.entries[0].constant_id!=0||plan.specialization.entries[0].offset!=0||
    plan.specialization.entries[0].size!=4 {
  return Err("R85 textured fixed-UI binding plan changed specialization or descriptor shape".into());}
 for (binding,stage,index) in [(0u32,Stage::Vertex,2u32),(1,Stage::Vertex,3),(2,Stage::Fragment,1),(3,Stage::Fragment,2)] {
  let Some(write)=plan.descriptors.iter().find(|w|w.set==0&&w.binding==binding) else{return Err("R85 textured fixed-UI buffer descriptor is missing".into());};
  if write.array_element!=0||write.ty.as_raw()!=6||
    !matches!(write.source,WriteSource::Buffer{slot} if slot.stage==stage&&slot.class==SlotClass::Buffer&&slot.index==index){
   return Err("R85 textured fixed-UI buffer descriptor source changed".into());}
 }
 let Some(constant)=plan.descriptors.iter().find(|w|w.set==0&&w.binding==4) else{return Err("R85 textured fixed-UI constant descriptor is missing".into());};
 if constant.array_element!=0||constant.ty.as_raw()!=7||!matches!(constant.source,WriteSource::ConstantBlob{stage:Stage::Fragment}){
  return Err("R85 textured fixed-UI constant descriptor source changed".into());}
 let Some(sampled)=plan.descriptors.iter().find(|w|w.set==0&&w.binding==5) else{return Err("R85 textured fixed-UI sampled descriptor is missing".into());};
 let sampled_slots=match sampled.source {
  WriteSource::CombinedImageSampler{
   image:ImageSource::Texture{slot:texture,..},sampler:SamplerSource::Slot{slot:sampler,compare:false,integer:false,..}}
    if sampled.array_element==0&&sampled.ty.as_raw()==1&&texture.stage==Stage::Fragment&&texture.class==SlotClass::Texture&&texture.index==3&&
       sampler.stage==Stage::Fragment&&sampler.class==SlotClass::Sampler&&sampler.index==0=>(texture,sampler),
  _=>return Err("R85 textured fixed-UI sampled descriptor source changed".into()),
 };
 let draw=metal2vulkan::bind::DrawCall::Primitives{vertex_start:d.vertex_start,vertex_count:d.vertex_count,
  instance_count:d.instance_count,base_instance:d.base_instance};
 let commands=plan.instantiate_draw(&state,&draw).map_err(|e|e.to_string())?;
 if !commands.descriptor_bytes.is_empty()||!commands.push_holes.is_empty()||!commands.root_tables.is_empty()||
    !commands.vertex_strides.is_empty()||!commands.uses.is_empty()||commands.indirect_needs_arguments||
    commands.index_overrun.is_some()||commands.index_bounds_pass||!commands.amplifications.is_empty()||
    commands.specialization.is_some()||commands.mesh_dispatch.is_some()||commands.descriptor_ranges.len()!=4 {
  return Err("R85 textured fixed-UI draw requires unsupported auxiliary state".into());}
 match commands.draw {vk::DrawCommand::Draw{vertex_count,instance_count,first_vertex,first_instance}
   if vertex_count==d.vertex_count&&instance_count==d.instance_count&&first_vertex==d.vertex_start&&first_instance==d.base_instance=>{},
  _=>return Err("R85 textured fixed-UI draw command changed form".into())}
 let mut out=FixedUiR85Plan::default();out.version=1;out.binding_count=4;
 for (i,resource) in exact[..4].iter().enumerate(){let binding=i as u32;
  let range=commands.descriptor_ranges.iter().find(|r|r.set==0&&r.binding==binding).ok_or("R85 textured fixed-UI descriptor range is missing")?;
  out.bindings[i]=FixedUiR85Binding{stage:resource.stage,index:resource.index,descriptor_set:0,binding,
   bind_offset:resource.offset,resource_length:resource.length,descriptor_offset:range.range.offset,descriptor_length:range.range.range};}
 let texture=exact[5];out.texture=FixedUiR85TextureBinding{texture_stage:2,texture_index:sampled_slots.0.index,
  sampler_stage:2,sampler_index:sampled_slots.1.index,descriptor_set:0,binding:5,format:texture.format,
  texture_type:texture.texture_type,width:texture.width,height:texture.height,resource_id:texture.resource_id};
 fixed_ui_r85_pack_pushes(commands.push_constants.iter().map(|p|(p.stages.as_raw(),p.offset,p.bytes.as_slice())),&mut out)?;
 let mut blobs=handle._pipeline.stages.iter().filter_map(|s|s.constant_blob.as_ref().map(|b|(s.stage.as_raw(),b)));
 let (stage,blob)=blobs.next().ok_or("R85 textured fixed-UI constant blob is missing")?;
 if blobs.next().is_some()||stage!=16||blob.as_ref()!=[0,126,0,126,0,126,0,126]{return Err("R85 textured fixed-UI constant blob changed".into());}
 out.constant_stage=stage;out.constant_length=8;out.constant_bytes.copy_from_slice(blob);
 out.specialization_id=0;out.specialization_length=4;out.specialization_bytes.copy_from_slice(&plan.specialization.data);Ok(out)
}

/* Reflection-driven execution projection for any single-target render
 * pipeline whose descriptors are buffers, the constant blob, sampled
 * textures, samplers and framebuffer-fetch input attachments.  Bindings are
 * the translator's set-0 descriptor writes in binding order; pushes are every
 * push-constant write of the draw.  Specialization must equal the module
 * defaults, which is what the Mali compile assumes. */
pub const GENERIC_BUFFER:u32=1;pub const GENERIC_CONSTANT:u32=2;pub const GENERIC_COMBINED:u32=3;
pub const GENERIC_COMBINED_IMMUTABLE:u32=4;pub const GENERIC_TEXTURE:u32=5;pub const GENERIC_SAMPLER:u32=6;
pub const GENERIC_ATTACHMENT:u32=7;
/* Statically used but unbound: Metal reads zeros, so a zero texture (or a
 * zero buffer) stands in, whatever the sampler. */
pub const GENERIC_NEUTRAL_IMAGE:u32=8;pub const GENERIC_NEUTRAL_BUFFER:u32=9;
/* WriteSource::ColorWriteTable: bind tegu_air_color_write_table's bytes. */
pub const GENERIC_COLOR_TABLE:u32=10;
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct GenericBinding {pub binding:u32,pub kind:u32,pub descriptor_type:u32,pub stage:u32,pub index:u32,
 pub sampler_stage:u32,pub sampler_index:u32,pub attachment:u32,
 /* Immutable sampler (Vulkan enums): min, mag, mipmap, address u/v/w, border. */
 pub min_filter:u32,pub mag_filter:u32,pub mipmap_mode:u32,pub address:[u32;3],pub border_color:u32,
 pub format:u32,pub texture_type:u32,pub width:u32,pub height:u32,pub reserved:u32,
 pub resource_id:u64,pub bind_offset:u64,pub resource_length:u64,pub descriptor_offset:u64,pub descriptor_length:u64}
#[repr(C)]
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub struct GenericPlan {pub version:u32,pub binding_count:u32,pub push_count:u32,pub push_bytes_length:u32,
 pub constant_stage:u32,pub constant_length:u32,pub reads_framebuffer:u32,pub reserved:u32,
 pub bindings:[GenericBinding;16],pub pushes:[FixedUiR85PushSpan;8],pub push_bytes:[u8;256],pub constant_bytes:[u8;512]}
impl Default for GenericPlan {fn default()->Self{Self{version:0,binding_count:0,push_count:0,push_bytes_length:0,
 constant_stage:0,constant_length:0,reads_framebuffer:0,reserved:0,bindings:[GenericBinding::default();16],
 pushes:[FixedUiR85PushSpan::default();8],push_bytes:[0;256],constant_bytes:[0;512]}}}
const _:()=assert!(size_of::<GenericBinding>()==120&&size_of::<GenericPlan>()==2848);

/* Additive graphics specialization sidecar.  GenericPlan v1 and every byte of
 * its layout stay unchanged; this type carries the exact per-stage SpecId
 * tables the specialized lowering entry consumes, plus the bound view facts
 * the generic plan deliberately does not restate. */
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct GraphicsSpecConstant {pub id:u32,pub size:u32,pub data:[u8;8]}
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct GraphicsSpecializationBinding {
 pub binding:u32,pub stage:u32,pub index:u32,pub kind:u32,
 pub view_format_vk:u32,pub view_format_metal:u32,
 pub compare_enabled:u32,pub view_type:u32,
 pub view_level:u32,pub view_layer:u32,pub flags:u32,pub aspect:u32,
}
pub const GRAPHICS_VIEW_TWIN:u32=1;
pub const GRAPHICS_VIEW_SHADER_SWIZZLE:u32=2;
/* The binding's sampler is an integer use (bind::sampler_desc integer). */
pub const GRAPHICS_SAMPLER_INTEGER:u32=4;
/* The view reads depth as colour (d, d, d, 1): descriptor swizzle R,R,R,1. */
pub const GRAPHICS_VIEW_DEPTH_AS_COLOR:u32=8;
pub const GRAPHICS_SPEC_MAX:usize=64;
#[repr(C)]
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub struct GraphicsSpecializationPlan {
 pub version:u32,pub binding_count:u32,pub vertex_count:u32,pub fragment_count:u32,
 pub specialization_length:u32,pub reserved:[u32;3],
 pub bindings:[GraphicsSpecializationBinding;16],
 pub vertex_constants:[GraphicsSpecConstant;GRAPHICS_SPEC_MAX],
 pub fragment_constants:[GraphicsSpecConstant;GRAPHICS_SPEC_MAX],
 pub specialization_bytes:[u8;512],
}
impl Default for GraphicsSpecializationPlan {fn default()->Self{Self{version:0,binding_count:0,
 vertex_count:0,fragment_count:0,specialization_length:0,reserved:[0;3],
 bindings:[GraphicsSpecializationBinding::default();16],
 vertex_constants:[GraphicsSpecConstant::default();GRAPHICS_SPEC_MAX],
 fragment_constants:[GraphicsSpecConstant::default();GRAPHICS_SPEC_MAX],
 specialization_bytes:[0;512]}}}
const _:()=assert!(size_of::<GraphicsSpecConstant>()==16&&
 size_of::<GraphicsSpecializationBinding>()==48&&
 size_of::<GraphicsSpecializationPlan>()==3360);

/* Colour formats n2r renders (nord2_metal_render.c n2r_rt_format): the
 * blendable tile formats (Mesa v9 pan_blendable_formats) and the raw ones
 * (opaque output only: blending, partial write masks and multisample
 * resolve refuse in blend_state.c / n2r). */
fn n2r_target_format(format:u32)->bool {
 matches!(format,1|10|11|25|30|31|40|42|43|65|70|71|80|81|90|92|94|115|
  12|13|14|20|22|23|24|32|33|34|53|54|55|60|62|63|64|72|73|74|91|103|104|105|110|112|113|114|123|124|125)
}
fn generic_target_formats(formats:impl IntoIterator<Item=u32>)->bool {
 /* One to four targets from the formats the per-target/true-MRT renderer
  * already handles.  QuartzCore's blur passes use two RGBA16F/BGR5A1 or
  * BGRA8+RGBA16F targets; the attachment order is explicit and each target
  * gets its own format/bpp/n2r job.  Format0 gaps count as unsupported
  * rather than being filled speculatively. */
 let mut count=0usize;
 for format in formats {
  if !n2r_target_format(format)||count==4{return false;}
  count+=1;
 }
 count!=0
}

/* Colour formats the arch9 descriptor table packs for sampling
 * (nord2_texture_resources.h n2t_format_query minus depth): every
 * uncompressed normalized/integer/float/packed/sRGB format, ETC2/EAC and ASTC
 * 2D LDR/sRGB/HDR. The frontend binds exactly these (tegu_generic_texture_ok);
 * gpu/tests/texture_format_frontend_test.py checks the two lists agree. The
 * translator still decides representability (sRGB 8-bit raw reads and
 * cross-class reads run on translator variants, VariantPipeline; formats the
 * profile lacks refuse by name). */
fn n2t_sampled_format(format:u32)->bool {
 matches!(format,1|10|11|12|13|14|20|22|23|24|25|30|31|32|33|34|40|42|43|53|54|55|60|62|63|64|65|
  70|71|72|73|74|80|81|90|91|92|93|94|103|104|105|110|112|113|114|115|123|124|125|
  170|172|174|176|178|179|180|181|182|183)||
 generic_astc_2d_format(format)||matches!(format,222..=226|228..=236)
}

/* ASTC LDR/sRGB 2D formats reachable from the installed corpus.  HDR ASTC is
 * deliberately absent: arch9 has no qualified transport for it. */
fn generic_astc_2d_format(format:u32)->bool {
 matches!(format,186|187|188|189|190|192|193|194|195|196|197|198|199|200|
               204|205|206|207|208|210|211|212|213|214|215|216|217|218)
}

/* One stage's declarations, filtered against the SAME plan key.  The plan key
 * is a pipeline-wide SpecId key; a stage declaration with no exact per-key
 * value may only borrow the key entry when every declaration of that id in
 * both stages has the same key, size and unread-bit mask.  A same-SpecId
 * collision with different meanings therefore refuses instead of taking the
 * first stage's value. */
fn graphics_stage_constants(pipe:&metal2vulkan::pipeline::RenderPipeline,plan:&BindingPlan,
 stage:vk::ShaderStage)->Result<Vec<GraphicsSpecConstant>,String>{
 let declarations:Vec<_>=pipe.stages.iter()
  .filter(|m|m.stage==stage).flat_map(|m|m.specialization.constants.iter()).collect();
 if declarations.len()>GRAPHICS_SPEC_MAX{return Err("too many graphics specialization declarations".into());}
 let mut ids=std::collections::BTreeSet::new();
 for c in &declarations {
  if !matches!(c.size,1|2|4|8)||!ids.insert(c.id) {
   return Err(format!("invalid or duplicate graphics specialization declaration {}",c.id));
  }
 }
 let all:Vec<_>=pipe.stages.iter().flat_map(|m|
  m.specialization.constants.iter().map(move |c|(m.stage,*c))).collect();
 let entry=|id:u32|->Result<Option<(u32,u32)>,String>{
  let mut found=None;
  for e in &plan.specialization.entries {
   if e.constant_id!=id{continue;}
   if found.is_some(){return Err(format!("duplicate planner specialization id {id}"));}
   found=Some((e.offset,e.size));
  }
  Ok(found)
 };
 let read_entry=|e:(u32,u32),size:u32|->Result<u64,String>{
  if e.1!=size||!matches!(size,1|2|4|8){return Err(format!("graphics specialization width mismatch at id {}",e.1));}
  let (o,n)=(e.0 as usize,size as usize);
  let bytes=plan.specialization.data.get(o..o.saturating_add(n))
   .ok_or("graphics specialization data is short")?;
  let mut raw=[0u8;8];raw[..n].copy_from_slice(bytes);Ok(u64::from_le_bytes(raw))
 };
 let mut out=Vec::with_capacity(declarations.len());
 for c in declarations {
  let entry_value=entry(c.id)?;
  let mut provided=false;let mut value=0u64;
  if let Some(v)=plan.specialized_value(c.key){value=v;provided=true;}
  else if let Some(e)=entry_value {
   let same=all.iter().filter(|(_,d)|d.id==c.id)
    .all(|(_,d)|d.key==c.key&&d.size==c.size&&d.unread_bits==c.unread_bits);
   if !same{return Err(format!("graphics specialization id {} is ambiguous across stages",c.id));}
   value=read_entry(e,c.size)?;provided=true;
  }
  if !provided{continue;}
  let bits=if c.size==8{u64::MAX}else{(1u64<<(c.size*8))-1};
  value&=bits&!c.unread_bits;
  let mut data=[0u8;8];data[..c.size as usize].copy_from_slice(&value.to_le_bytes()[..c.size as usize]);
  out.push(GraphicsSpecConstant{id:c.id,size:c.size,data});
 }
 out.sort_by_key(|c|c.id);
 Ok(out)
}

fn graphics_binding_view(plan:&BindingPlan,binding:u32)
 ->Option<metal2vulkan::bind::ImageViewDesc>{
 let w=plan.descriptors.iter().find(|w|w.set==0&&w.binding==binding&&w.array_element==0)?;
 match &w.source {
  metal2vulkan::bind::WriteSource::CombinedImageSampler{
   image:metal2vulkan::bind::ImageSource::Texture{view,..},..}=>Some(*view),
  metal2vulkan::bind::WriteSource::Texture{view,..}=>Some(*view),
  _=>None,
 }
}

fn graphics_binding_compare(pipe:&metal2vulkan::pipeline::RenderPipeline,plan:&BindingPlan,binding:u32)->u32{
 let Some(w)=plan.descriptors.iter().find(|w|w.set==0&&w.binding==binding&&w.array_element==0) else{return 0;};
 match &w.source {
  metal2vulkan::bind::WriteSource::CombinedImageSampler{
   sampler:metal2vulkan::bind::SamplerSource::Slot{compare,..},..}=>u32::from(*compare),
  metal2vulkan::bind::WriteSource::Sampler{compare,..}=>u32::from(*compare),
  metal2vulkan::bind::WriteSource::CombinedImageSampler{
   sampler:metal2vulkan::bind::SamplerSource::Immutable,..}=>{
   let desc=pipe.layout.sets.iter().filter(|s|s.set==0)
    .flat_map(|s|s.bindings.iter()).find(|b|b.binding==binding)
    .and_then(|b|b.immutable_sampler.as_ref());
   u32::from(desc.is_some_and(|d|d.compare.is_some()))
  },
  _=>0,
 }
}

fn graphics_binding_integer(plan:&BindingPlan,binding:u32)->bool{
 let Some(w)=plan.descriptors.iter().find(|w|w.set==0&&w.binding==binding&&w.array_element==0) else{return false;};
 matches!(&w.source,metal2vulkan::bind::WriteSource::CombinedImageSampler{
   sampler:metal2vulkan::bind::SamplerSource::Slot{integer:true,..},..}|
  metal2vulkan::bind::WriteSource::Sampler{integer:true,..})
}

fn graphics_specialization_plan(pipe:&metal2vulkan::pipeline::RenderPipeline,plan:&BindingPlan,
 resources:&[RenderResource],generic:&GenericPlan)->Result<GraphicsSpecializationPlan,String>{
 let vertex=graphics_stage_constants(pipe,plan,vk::ShaderStage::VERTEX)?;
 let fragment=graphics_stage_constants(pipe,plan,vk::ShaderStage::FRAGMENT)?;
 let mut out=GraphicsSpecializationPlan::default();out.version=1;
 out.vertex_count=vertex.len() as u32;out.fragment_count=fragment.len() as u32;
 out.vertex_constants[..vertex.len()].copy_from_slice(&vertex);
 out.fragment_constants[..fragment.len()].copy_from_slice(&fragment);
 if plan.specialization.data.len()>out.specialization_bytes.len(){
  return Err("graphics specialization key exceeds ABI".into());
 }
 out.specialization_length=plan.specialization.data.len() as u32;
 out.specialization_bytes[..plan.specialization.data.len()].copy_from_slice(&plan.specialization.data);
 out.binding_count=generic.binding_count;
 for (i,b) in generic.bindings[..generic.binding_count as usize].iter().enumerate() {
  let (stage,index)=if b.kind==GENERIC_SAMPLER{(b.sampler_stage,b.sampler_index)}else{(b.stage,b.index)};
  let mut o=GraphicsSpecializationBinding{binding:b.binding,stage,index,kind:b.kind,
   compare_enabled:graphics_binding_compare(pipe,plan,b.binding),..Default::default()};
  if matches!(b.kind,GENERIC_COMBINED|GENERIC_SAMPLER)&&graphics_binding_integer(plan,b.binding){o.flags|=GRAPHICS_SAMPLER_INTEGER;}
  if matches!(b.kind,GENERIC_COMBINED|GENERIC_COMBINED_IMMUTABLE|GENERIC_TEXTURE) {
   let _=resources.iter().find(|r|r.kind==2&&r.stage==stage&&r.index==index)
    .ok_or("specialization texture is not bound")?;
   let view=graphics_binding_view(plan,b.binding)
    .ok_or("specialization texture has no exact selected view")?;
   /* A depth read without comparison maps (d, d, d, 1) (translator
    * DEPTH_AS_COLOR, Metal's depth-through-colour read): the Mali texture
    * descriptor swizzle R,R,R,1 reproduces it (GRAPHICS_VIEW_DEPTH_AS_COLOR). */
   let depth_as_color=view.aspect==vk::ImageAspectFlags::DEPTH&&
    matches!(view.components.r,vk::ComponentSwizzle::IDENTITY|vk::ComponentSwizzle::R)&&
    view.components.g==vk::ComponentSwizzle::R&&view.components.b==vk::ComponentSwizzle::R&&
    view.components.a==vk::ComponentSwizzle::ONE;
   if !view.shader_swizzle&&view.components!=vk::ComponentMapping::IDENTITY&&!depth_as_color {
    return Err("graphics texture view component mapping is not representable".into());
   }
   if !view.shader_swizzle&&depth_as_color{o.flags|=GRAPHICS_VIEW_DEPTH_AS_COLOR;}
   let metal_view=metal_view_format(view.format)?;
   o.view_format_metal=metal_view;o.view_format_vk=view.format.as_raw() as u32;
   o.view_type=view.view_type.as_raw() as u32;
   o.view_level=view.level.unwrap_or(0);o.view_layer=0;
   o.aspect=view.aspect.as_raw() as u32;
   if view.twin{o.flags|=GRAPHICS_VIEW_TWIN;}
   if view.shader_swizzle{o.flags|=GRAPHICS_VIEW_SHADER_SWIZZLE;}
  }
  out.bindings[i]=o;
 }
 Ok(out)
}

fn generic_plan(handle:&RenderPipelineHandle,resources:&[RenderResource],layouts:&[VertexLayout],d:&RenderDraw)->Result<GenericPlan,String>{
 generic_plan_mode(handle,resources,layouts,d,false).map(|(plan,_)|plan)
}
fn generic_plan_specialized(handle:&RenderPipelineHandle,resources:&[RenderResource],layouts:&[VertexLayout],d:&RenderDraw)->Result<(GenericPlan,GraphicsSpecializationPlan),String>{
 let (plan,specialization)=generic_plan_mode(handle,resources,layouts,d,true)?;
 Ok((plan,specialization.ok_or("generic specialization plan is missing")?))
}
fn generic_plan_mode(handle:&RenderPipelineHandle,resources:&[RenderResource],layouts:&[VertexLayout],d:&RenderDraw,allow_specialization:bool)->Result<(GenericPlan,Option<GraphicsSpecializationPlan>),String>{
 use metal2vulkan::{bind::{BoundBuffer,ImageSource,SamplerSource,WriteSource},pipeline::{SlotClass,Stage}};
 /* Match the renderer's per-target/true-MRT format contract.  Attachment
  * order is preserved; this gate does not qualify an unknown format or more
  * than four targets. */
 if !generic_target_formats(handle.requested_attachments.iter().map(|a|a.format)) {
  return Err("generic plan requires one to four n2r colour targets".into());}
 if d.version!=1||d.vertex_count==0||d.vertex_count>(1<<24)||d.instance_count==0||d.instance_count>(1<<20)||
    d.vertex_start.checked_add(d.vertex_count).is_none()||d.base_instance.checked_add(d.instance_count).is_none(){
  return Err("invalid or unbounded generic direct draw range".into());}
 let mut state=compositor_plan_state(resources,allow_specialization)?;
 let vertex_end=(d.vertex_start+d.vertex_count) as u64;let instance_end=(d.base_instance+d.instance_count) as u64;
 for layout in layouts {
  if layout.stride==0||layout.stride>4096||layout.index>=31||!(1..=2).contains(&layout.step_function)||
     (layout.step_function==2&&layout.step_rate!=1){return Err("generic vertex layout unsupported".into());}
  let resource=resources.iter().find(|r|r.kind==1&&r.stage==1&&r.index==layout.index)
   .ok_or("generic vertex buffer is not bound")?;
  let count=if layout.step_function==1{vertex_end}else{instance_end};
  let bytes=count.checked_mul(layout.stride as u64).ok_or("generic vertex byte range overflow")?;
  if resource.offset.checked_add(bytes).is_none_or(|end|end>resource.length){
   return Err("generic vertex fetch exceeds its retained allocation".into());}
  let vertex_stage=state.stages.get_mut(&Stage::Vertex).ok_or("generic vertex stage missing")?;
  vertex_stage.buffers.insert(layout.index,BoundBuffer::Buffer{length:bytes,offset:0,gpu_address:0,
   device_address:None,attribute_stride:Some(layout.stride)});
 }
 let mut plan_ref=handle.cached_plan(&state)?;
 /* A plan that names a translator variant (sRGB raw views, cross-class
  * reads, ...) is re-made on the variant pipeline, which must name the same
  * variant; everything below then comes from that pipeline. The
  * specialization plan carries its id (reserved[0]=1, reserved[1..2] = key
  * hash) and the frontend compiles tegu_air_render_pipeline_variant_spirv. */
 let mut variant:Option<std::sync::Arc<VariantPipeline>>=None;
 if !plan_ref.variant.is_empty() {
  if !allow_specialization{return Err("generic plan requires a translator variant".into());}
  let v=handle.variant_for(&plan_ref.variant)?;
  let vplan=v.plan(&handle._device,&state)?;
  if vplan.variant!=v.key{return Err("translator variant plan names another variant".into());}
  plan_ref=vplan;variant=Some(v);
 }
 let pipe:&metal2vulkan::pipeline::RenderPipeline=match &variant{Some(v)=>&v.pipeline,None=>&handle._pipeline};
 let plan=&*plan_ref;
 let draw=metal2vulkan::bind::DrawCall::Primitives{vertex_start:d.vertex_start,vertex_count:d.vertex_count,
  instance_count:d.instance_count,base_instance:d.base_instance};
 let commands=plan.instantiate_draw(&state,&draw).map_err(|e|e.to_string())?;
 if !commands.descriptor_bytes.is_empty()||!commands.push_holes.is_empty()||!commands.root_tables.is_empty()||
    !commands.uses.is_empty()||commands.indirect_needs_arguments||commands.index_overrun.is_some()||
    commands.index_bounds_pass||!commands.amplifications.is_empty()||commands.specialization.is_some()||
    commands.mesh_dispatch.is_some(){return Err("generic draw requires unsupported auxiliary state".into());}
 match commands.draw {vk::DrawCommand::Draw{vertex_count,instance_count,first_vertex,first_instance}
   if vertex_count==d.vertex_count&&instance_count==d.instance_count&&first_vertex==d.vertex_start&&first_instance==d.base_instance=>{},
  _=>return Err("generic draw command changed form".into())}
 let stage_id=|s:Stage|match s {Stage::Vertex=>Ok(1u32),Stage::Fragment=>Ok(2u32),_=>Err("generic slot stage unsupported".to_owned())};
 let mut out=GenericPlan::default();out.version=1;
 let mut writes:Vec<_>=plan.descriptors.iter().collect();writes.sort_by_key(|w|(w.set,w.binding));
 if writes.len()>out.bindings.len(){return Err("generic plan has too many descriptors".into());}
 let layout_binding=|binding:u32|pipe.layout.sets.iter().filter(|s|s.set==0)
  .flat_map(|s|s.bindings.iter()).find(|b|b.binding==binding);
 for (i,w) in writes.iter().enumerate() {
  if w.set!=0||w.array_element!=0{return Err("generic descriptor outside set 0 element 0".into());}
  let mut b=GenericBinding{binding:w.binding,descriptor_type:w.ty.as_raw() as u32,..Default::default()};
  let texture_facts=|b:&mut GenericBinding|->Result<(),String>{
   let r=resources.iter().find(|r|r.kind==2&&r.stage==b.stage&&r.index==b.index).ok_or("generic texture is not bound")?;
   b.format=r.format;b.texture_type=r.texture_type;b.width=r.width;b.height=r.height;b.resource_id=r.resource_id;Ok(())};
  match &w.source {
   WriteSource::Buffer{slot}|WriteSource::InlineBytes{slot} if slot.class==SlotClass::Buffer=>{
    b.kind=GENERIC_BUFFER;b.stage=stage_id(slot.stage)?;b.index=slot.index;
    let r=resources.iter().find(|r|r.kind==1&&r.stage==b.stage&&r.index==slot.index).ok_or("generic buffer is not bound")?;
    let range=commands.descriptor_ranges.iter().find(|r|r.set==0&&r.binding==w.binding).ok_or("generic descriptor range is missing")?;
    b.bind_offset=r.offset;b.resource_length=r.length;b.descriptor_offset=range.range.offset;b.descriptor_length=range.range.range;
    // Descriptor ranges are allocation-relative (the bind offset's aligned
    // base; the residual travels as a push field).
    if range.range.offset.checked_add(range.range.range).is_none_or(|end|end>r.length){
     return Err("generic buffer descriptor exceeds its retained allocation".into());}
   },
   WriteSource::ConstantBlob{stage}=>{b.kind=GENERIC_CONSTANT;b.stage=stage_id(*stage)?;},
   WriteSource::CombinedImageSampler{image:ImageSource::Texture{slot,..},sampler} if slot.class==SlotClass::Texture=>{
    b.stage=stage_id(slot.stage)?;b.index=slot.index;texture_facts(&mut b)?;
    match sampler {
     /* An integer use (the texture's class is integer) carries
      * GRAPHICS_SAMPLER_INTEGER in the specialization sidecar, which applies
      * vk_sampler's integer rules; without the sidecar it refuses. */
     SamplerSource::Slot{slot:s,integer,..} if s.class==SlotClass::Sampler=>{
      if *integer&&!allow_specialization{return Err("generic integer sampler requires the specialization sidecar".into());}
      b.kind=GENERIC_COMBINED;b.sampler_stage=stage_id(s.stage)?;b.sampler_index=s.index;},
     SamplerSource::Immutable=>{
      let desc=layout_binding(w.binding).and_then(|l|l.immutable_sampler.as_ref()).ok_or("generic immutable sampler is missing")?;
      if desc.compare.is_some()||desc.max_anisotropy.is_some(){return Err("generic immutable sampler unsupported".into());}
      b.kind=GENERIC_COMBINED_IMMUTABLE;b.min_filter=desc.min_filter.as_raw() as u32;b.mag_filter=desc.mag_filter.as_raw() as u32;
      b.mipmap_mode=desc.mipmap_mode.as_raw() as u32;b.border_color=desc.border_color.as_raw() as u32;
      b.address=[desc.address_mode_u.as_raw() as u32,desc.address_mode_v.as_raw() as u32,desc.address_mode_w.as_raw() as u32];},
     _=>return Err("generic combined sampler source unsupported".into()),
    }
   },
   WriteSource::Texture{slot,..} if slot.class==SlotClass::Texture=>{
    b.kind=GENERIC_TEXTURE;b.stage=stage_id(slot.stage)?;b.index=slot.index;texture_facts(&mut b)?;},
   WriteSource::Sampler{slot,integer,..} if slot.class==SlotClass::Sampler=>{
    if *integer&&!allow_specialization{return Err("generic integer sampler requires the specialization sidecar".into());}
    b.kind=GENERIC_SAMPLER;b.sampler_stage=stage_id(slot.stage)?;b.sampler_index=slot.index;},
   WriteSource::CombinedImageSampler{image:ImageSource::Neutral(_),..}=>{b.kind=GENERIC_NEUTRAL_IMAGE;b.stage=2;},
   WriteSource::Neutral(_) if matches!(w.ty.as_raw(),1|2|8)=>{b.kind=GENERIC_NEUTRAL_IMAGE;b.stage=2;},
   WriteSource::Neutral(_) if matches!(w.ty.as_raw(),6|7)=>{b.kind=GENERIC_NEUTRAL_BUFFER;},
   /* The fragment stage's sRGB colour table (Metal's exact 12-bit sRGB
    * encode of a float written to an sRGB attachment): a buffer holding
    * exactly metal2vulkan::bind::color_write_table(), bound whole as the
    * write's descriptor type (uniform or storage). */
   WriteSource::ColorWriteTable if matches!(w.ty.as_raw(),6|7)=>{
    let n=metal2vulkan::bind::color_write_table().len() as u64;
    b.kind=GENERIC_COLOR_TABLE;b.stage=2;b.resource_length=n;b.descriptor_offset=0;b.descriptor_length=n;},
   WriteSource::Attachment{color_index}=>{
    if *color_index as usize>=handle.requested_attachments.len(){
     return Err("generic framebuffer fetch reads an absent color attachment".into());}
    b.kind=GENERIC_ATTACHMENT;b.stage=2;b.attachment=*color_index;out.reads_framebuffer=1;},
   other=>return Err(format!("generic descriptor binding {} source unsupported: {:?}",w.binding,other).chars().take(300).collect()),
  }
  out.bindings[i]=b;
 }
 out.binding_count=writes.len() as u32;
 let mut pushes:Vec<_>=commands.push_constants.iter().map(|p|(p.stages.as_raw() as u32,p.offset,p.bytes.as_slice())).collect();
 pushes.sort_by_key(|(stage,offset,_)|(*stage,*offset));
 if pushes.len()>out.pushes.len(){return Err("generic draw has too many push writes".into());}
 let mut cursor=0usize;
 for (i,(stage,offset,bytes)) in pushes.into_iter().enumerate(){let end=cursor+bytes.len();
  if end>out.push_bytes.len(){return Err("generic push bytes exceed ABI".into());}
  out.push_bytes[cursor..end].copy_from_slice(bytes);
  out.pushes[i]=FixedUiR85PushSpan{stage,offset,length:bytes.len() as u32,bytes_offset:cursor as u32};cursor=end;out.push_count+=1;}
 out.push_bytes_length=cursor as u32;
 let mut blobs=pipe.stages.iter().filter_map(|s|s.constant_blob.as_ref().map(|b|(s.stage.as_raw(),b)));
 if let Some((stage,blob))=blobs.next(){
  if blobs.next().is_some()||blob.len()>out.constant_bytes.len(){return Err("generic constant blob unsupported".into());}
  out.constant_stage=stage;out.constant_length=blob.len() as u32;out.constant_bytes[..blob.len()].copy_from_slice(blob);}
 if out.bindings[..out.binding_count as usize].iter().any(|b|b.kind==GENERIC_CONSTANT)&&out.constant_length==0{
  return Err("generic constant descriptor has no blob".into());}
 if allow_specialization {
  let mut spec=graphics_specialization_plan(pipe,&plan,resources,&out)?;
  if let Some(v)=&variant{spec.reserved=[1,v.hash as u32,(v.hash>>32) as u32];}
  return Ok((out,Some(spec)));
 }
 for (stage,constants) in [(vk::ShaderStage::VERTEX,graphics_stage_constants(pipe,&plan,vk::ShaderStage::VERTEX)?),
                           (vk::ShaderStage::FRAGMENT,graphics_stage_constants(pipe,&plan,vk::ShaderStage::FRAGMENT)?)] {
  for value in constants {
   let c=pipe.stages.iter().find(|m|m.stage==stage)
    .and_then(|m|m.specialization.constants.iter().find(|c|c.id==value.id))
    .ok_or("generic specialization constant is unknown")?;
   let bits=if value.size==8{u64::MAX}else{(1u64<<(value.size*8))-1};
   let got=u64::from_le_bytes(value.data)&bits&!c.unread_bits;
   if (got^(c.default&bits&!c.unread_bits))!=0 {
    return Err(format!("generic draw specializes constant {} away from its default",value.id));
   }
  }
 }
 Ok((out,None))
}

fn typed_usage_add(out:&mut RenderTypedPlan,kind:u32,stage:u32,index:u32,class_code:u32,
 flags:u32,range:Option<(u64,u64)>)->Result<(),String>{
 if kind==0||kind>3||!(1..=2).contains(&stage)||index>=if kind==3{16}else{31}||
    (kind==3&&class_code!=0)||(kind!=3&&!(1..=8).contains(&class_code)) {
  return Err("typed use outside supported stage/kind/class slots".into());
 }
 if let Some((offset,length))=range{offset.checked_add(length).ok_or("typed range overflow")?;}
 let class_mask=if class_code==0{0}else{1u32<<(class_code-1)};
 if let Some(i)=out.uses[..out.count as usize].iter().position(|u|u.kind==kind&&u.stage==stage&&u.index==index){
  let entry=&mut out.uses[i];entry.flags|=flags;entry.class_mask|=class_mask;
  if kind!=3 {
   if range.is_none(){entry.flags=(entry.flags&!4)|32;entry.range_offset=0;entry.range_length=0;}
   else if entry.flags&32==0 {
    let (offset,length)=range.unwrap();let end=offset.checked_add(length).ok_or("typed range overflow")?;
    if entry.flags&4==0{entry.range_offset=offset;entry.range_length=length;entry.flags|=4;}
    else {let old_end=entry.range_offset.checked_add(entry.range_length).ok_or("typed range overflow")?;
     let start=entry.range_offset.min(offset);entry.range_offset=start;entry.range_length=old_end.max(end)-start;}
   }
  }
  return Ok(());
 }
 if out.count as usize>=out.uses.len(){return Err("typed usage count exceeds ABI".into());}
 let (offset,length,range_flag)=if kind==3{(0,0,0)}else{match range {
  Some((o,n))=>(o,n,4),None=>(0,0,32)}};
 out.uses[out.count as usize]=RenderResourceUse{kind,stage,index,class_mask,
  flags:flags|range_flag,reserved:0,range_offset:offset,range_length:length};
 out.count+=1;Ok(())
}

fn render_typed_plan(handle:&RenderPipelineHandle,resources:&[RenderResource],d:&RenderDraw)->Result<RenderTypedPlan,String>{
 use metal2vulkan::{bind::{AccessClass,UseTarget,WriteSource},pipeline::{RuntimeFact,SlotClass,Stage}};
 let state=compositor_plan_state(resources,false)?;
 let plan_ref=handle.cached_plan(&state)?;let plan=&*plan_ref;
 if !plan.variant.is_empty(){return Err("typed draw plan requires an unsupported variant".into());}
 let draw=metal2vulkan::bind::DrawCall::Primitives{vertex_start:d.vertex_start,vertex_count:d.vertex_count,
  instance_count:d.instance_count,base_instance:d.base_instance};
 let _commands=plan.instantiate_draw(&state,&draw).map_err(|e|e.to_string())?;
 let mut out=RenderTypedPlan::default();out.version=1;
 out.attachment_count=handle.requested_attachments.len() as u32;
 if out.attachment_count>8{return Err("typed plan attachment count exceeds ABI".into());}
 for (i,a) in handle.requested_attachments.iter().enumerate(){out.attachments[i]=*a;}
 out.diagnostic_count=plan.diagnostics.len() as u32;
 out.neutral_count=plan.descriptors.iter().filter(|w|matches!(&w.source,WriteSource::Neutral(_))).count() as u32;
 let stage_id=|stage:Stage|match stage {Stage::Vertex=>1,Stage::Fragment=>2,_=>0};
 let kind_id=|class:SlotClass|match class {SlotClass::Buffer=>1,SlotClass::Texture=>2,SlotClass::Sampler=>3};
 for usage in &plan.uses {
  let UseTarget::Slot(slot)=usage.resource else{return Err("typed compositor plan has a non-slot resource use".into());};
  let range=usage.range.map(|r|(r.offset,r.len));
  let flags=(usage.access.read as u32)|((usage.access.write as u32)<<1);
  let class=match usage.class {AccessClass::Storage=>1,AccessClass::Uniform=>2,
   AccessClass::Sampled=>3,AccessClass::StorageImage=>4,AccessClass::VertexInput=>5,
   AccessClass::Index=>6,AccessClass::Indirect=>7,AccessClass::Attachment=>8,
   _=>return Err("unknown typed access class".to_owned())};
  typed_usage_add(&mut out,kind_id(slot.class),stage_id(slot.stage),slot.index,class,flags,range)?;
 }
 for dep in &handle._pipeline.dependencies {
  match dep.fact {
   RuntimeFact::SamplerCoordinates{slot}=>{if slot.class==SlotClass::Sampler{typed_usage_add(&mut out,3,stage_id(slot.stage),slot.index,0,8,None)?;}},
   RuntimeFact::SamplerReduction{slot}=>{if slot.class==SlotClass::Sampler{typed_usage_add(&mut out,3,stage_id(slot.stage),slot.index,0,16,None)?;}},
   _=>{}
  }
 }
 Ok(out)
}

/// Additive typed companion to `plan_draw`: returns the JSON audit plan plus a
/// C-stable typed usage/attachment snapshot directly from translator values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_plan_draw_typed(
 pipeline:*const RenderPipelineHandle,resources:*const RenderResource,count:usize,draw:*const RenderDraw,
 out_json:*mut Blob,out_typed:*mut RenderTypedPlan,error:*mut c_char,cap:usize)->i32{
 if out_json.is_null()||out_typed.is_null(){return -1;}
 let json_addr=out_json as usize;let typed_addr=out_typed as usize;let err_addr=error as usize;
 let input_spans=[(resources as usize,if resources.is_null(){0}else{count.saturating_mul(size_of::<RenderResource>())}),
  (draw as usize,if draw.is_null(){0}else{size_of::<RenderDraw>()}),
  (pipeline as usize,if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()})];
 if compositor_ranges_overlap(json_addr,size_of::<Blob>(),typed_addr,size_of::<RenderTypedPlan>())||
    compositor_ranges_overlap(json_addr,size_of::<Blob>(),err_addr,cap)||
    compositor_ranges_overlap(typed_addr,size_of::<RenderTypedPlan>(),err_addr,cap)||
    input_spans.iter().any(|(p,n)|compositor_ranges_overlap(json_addr,size_of::<Blob>(),*p,*n)||
      compositor_ranges_overlap(typed_addr,size_of::<RenderTypedPlan>(),*p,*n)||compositor_ranges_overlap(err_addr,cap,*p,*n)){return -1;}
 unsafe{*out_json=Blob{bytes:ptr::null_mut(),length:0};*out_typed=RenderTypedPlan::default();}
 let status=unsafe{tegu_air_render_pipeline_plan_draw(pipeline,resources,count,draw,out_json,error,cap)};
 if status!=0{return status;}
 let typed=unsafe{attempt(error,cap,||{
  if pipeline.is_null()||draw.is_null()||count>186||(count>0&&resources.is_null()){return Err("invalid typed render usage inputs".into());}
  let input=if count==0 {&[]} else {slice::from_raw_parts(resources,count)};
  render_typed_plan(&*pipeline,input,&*draw)
 })};
 match typed {Some(value)=>{unsafe{*out_typed=value};0},None=>{unsafe{tegu_air_blob_free(out_json);*out_json=Blob{bytes:ptr::null_mut(),length:0};} -1}}
}

/// Execution-facing plan for the one exact R80 fixed-UI contract. Unlike the
/// generic typed planner, success means every draw-varying descriptor range,
/// residual push byte and stage constant needed by this class is represented.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_plan_fixed_ui_r80(
 pipeline:*const RenderPipelineHandle,resources:*const RenderResource,count:usize,draw:*const RenderDraw,
 out:*mut FixedUiR80Plan,error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe{error_copy(error,cap,"null R80 fixed-UI plan output")};return -1;}
 let out_addr=out as usize;let err_addr=error as usize;
 let input_spans=[(resources as usize,if resources.is_null(){0}else{count.saturating_mul(size_of::<RenderResource>())}),
  (draw as usize,if draw.is_null(){0}else{size_of::<RenderDraw>()}),
  (pipeline as usize,if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()})];
 if compositor_ranges_overlap(out_addr,size_of::<FixedUiR80Plan>(),err_addr,cap)||
    input_spans.iter().any(|(p,n)|compositor_ranges_overlap(out_addr,size_of::<FixedUiR80Plan>(),*p,*n)||
      compositor_ranges_overlap(err_addr,cap,*p,*n)){return -1;}
 unsafe{*out=FixedUiR80Plan::default();}
 match unsafe{attempt(error,cap,||{
  if pipeline.is_null()||draw.is_null()||count!=2||resources.is_null(){return Err("invalid R80 fixed-UI plan inputs".into());}
  fixed_ui_r80_plan(&*pipeline,slice::from_raw_parts(resources,count),&*draw)
 })}{Some(value)=>{unsafe{*out=value};0},None=>-1}
}

/// Execution-facing plan for the exact R85 sampled fixed-UI contract. Success
/// represents all four descriptor buffer ranges, the combined texture/sampler
/// slots, resolved pushes, stage constant and specialization bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_plan_fixed_ui_r85_texture(
 pipeline:*const RenderPipelineHandle,resources:*const RenderResource,count:usize,draw:*const RenderDraw,
 out:*mut FixedUiR85Plan,error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe{error_copy(error,cap,"null R85 textured fixed-UI plan output")};return -1;}
 let out_addr=out as usize;let err_addr=error as usize;
 let input_spans=[(resources as usize,if resources.is_null(){0}else{count.saturating_mul(size_of::<RenderResource>())}),
  (draw as usize,if draw.is_null(){0}else{size_of::<RenderDraw>()}),
  (pipeline as usize,if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()})];
 if compositor_ranges_overlap(out_addr,size_of::<FixedUiR85Plan>(),err_addr,cap)||
    input_spans.iter().any(|(p,n)|compositor_ranges_overlap(out_addr,size_of::<FixedUiR85Plan>(),*p,*n)||
      compositor_ranges_overlap(err_addr,cap,*p,*n)){return -1;}
 unsafe{*out=FixedUiR85Plan::default();}
 match unsafe{attempt(error,cap,||{
  if pipeline.is_null()||draw.is_null()||count!=7||resources.is_null(){return Err("invalid R85 textured fixed-UI plan inputs".into());}
  fixed_ui_r85_plan(&*pipeline,slice::from_raw_parts(resources,count),&*draw)
 })}{Some(value)=>{unsafe{*out=value};0},None=>-1}
}

/// Reflection-driven projection of a direct draw (see `generic_plan`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_plan_generic(
 pipeline:*const RenderPipelineHandle,resources:*const RenderResource,count:usize,
 layouts:*const VertexLayout,layout_count:usize,draw:*const RenderDraw,
 out:*mut GenericPlan,error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe{error_copy(error,cap,"null generic plan output")};return -1;}
 let out_addr=out as usize;let err_addr=error as usize;
 let input_spans=[(resources as usize,if resources.is_null(){0}else{count.saturating_mul(size_of::<RenderResource>())}),
  (layouts as usize,if layouts.is_null(){0}else{layout_count.saturating_mul(size_of::<VertexLayout>())}),
  (draw as usize,if draw.is_null(){0}else{size_of::<RenderDraw>()}),
  (pipeline as usize,if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()})];
 if compositor_ranges_overlap(out_addr,size_of::<GenericPlan>(),err_addr,cap)||
    input_spans.iter().any(|(p,n)|compositor_ranges_overlap(out_addr,size_of::<GenericPlan>(),*p,*n)||
      compositor_ranges_overlap(err_addr,cap,*p,*n)){return -1;}
 unsafe{*out=GenericPlan::default();}
 match unsafe{attempt(error,cap,||{
  if pipeline.is_null()||draw.is_null()||(count!=0&&resources.is_null())||count>186||
     (layout_count!=0&&layouts.is_null())||layout_count>8{return Err("invalid generic plan inputs".into());}
  let resources=if count==0{&[][..]}else{slice::from_raw_parts(resources,count)};
  let layouts=if layout_count==0{&[][..]}else{slice::from_raw_parts(layouts,layout_count)};
  generic_plan(&*pipeline,resources,layouts,&*draw)
 })}{Some(value)=>{unsafe{*out=value};0},None=>-1}
}

/// Additive specialization companion.  Returns the same GenericPlan bytes as
/// the entry above plus the exact per-stage SpecId tables and bound view facts.
/// It admits real linear/ASTC sampler facts; callers must pass the tables to
/// the specialized graphics lowering entry, never to the unspecialized one.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_plan_generic_specialized(
 pipeline:*const RenderPipelineHandle,resources:*const RenderResource,count:usize,
 layouts:*const VertexLayout,layout_count:usize,draw:*const RenderDraw,
 out:*mut GenericPlan,specialization:*mut GraphicsSpecializationPlan,
 error:*mut c_char,cap:usize)->i32 {
 if out.is_null()||specialization.is_null(){unsafe{error_copy(error,cap,"null generic specialization output")};return -1;}
 let out_addr=out as usize;let spec_addr=specialization as usize;let err_addr=error as usize;
 let input_spans=[(resources as usize,if resources.is_null(){0}else{count.saturating_mul(size_of::<RenderResource>())}),
  (layouts as usize,if layouts.is_null(){0}else{layout_count.saturating_mul(size_of::<VertexLayout>())}),
  (draw as usize,if draw.is_null(){0}else{size_of::<RenderDraw>()}),
  (pipeline as usize,if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()})];
 let out_overlaps=|addr:usize,size:usize|->bool{
  compositor_ranges_overlap(addr,size,err_addr,cap)||
  input_spans.iter().any(|(p,n)|compositor_ranges_overlap(addr,size,*p,*n))};
 if compositor_ranges_overlap(out_addr,size_of::<GenericPlan>(),spec_addr,size_of::<GraphicsSpecializationPlan>())||
    out_overlaps(out_addr,size_of::<GenericPlan>())||
    out_overlaps(spec_addr,size_of::<GraphicsSpecializationPlan>()){return -1;}
 unsafe{*out=GenericPlan::default();*specialization=GraphicsSpecializationPlan::default();}
 match unsafe{attempt(error,cap,||{
  if pipeline.is_null()||draw.is_null()||(count!=0&&resources.is_null())||count>186||
     (layout_count!=0&&layouts.is_null())||layout_count>8{return Err("invalid generic specialization inputs".into());}
  let resources=if count==0{&[][..]}else{slice::from_raw_parts(resources,count)};
  let layouts=if layout_count==0{&[][..]}else{slice::from_raw_parts(layouts,layout_count)};
  generic_plan_specialized(&*pipeline,resources,layouts,&*draw)
 })}{Some((generic,spec))=>{unsafe{*out=generic;*specialization=spec};0},None=>-1}
}

/* Buffer-offset templates for the frontend plan memo (render_pipeline.inc
 * tegu_plan_memo_rebase).  A generic plan depends on a buffer record outside
 * the vertex layouts only through (a) its bind shape (length_class and
 * offset_alignment of metal2vulkan::bind::state), (b) instantiation's
 * descriptor range and SlotBindOffset residual of a descriptor-form slot
 * (BufferSlotTemplate::descriptor_range), (c) a SlotGuestBase field (gpu
 * address 0 + offset: compositor_plan_inputs_impl admits no guest address),
 * (d) the GenericBinding bind_offset/resource_length/descriptor_offset/
 * descriptor_length and generic_plan_mode's containment check, and (e) the
 * uniform-view aliasing check against written slots.  This entry restates
 * (b)-(d) per used slot of a plan already made for exactly these inputs and
 * refuses (-1) whatever it does not restate: a written or device-address
 * slot, a slot that is also a vertex layout slot, a push field outside the
 * plan's push spans, a buffer binding no template covers.  It re-plans and
 * requires `given` to be that plan byte for byte, and checks every restated
 * value against it.  The frontend keeps (a) in its key and probes each
 * template once with a moved offset against a fresh plan before using it. */
#[repr(C)]
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub struct GenericBufferTemplate {pub stage:u32,pub index:u32,pub binding:u32,pub window_kind:u32,
 pub window_start:i64,pub window_len:u64,pub alignment:u64,pub max_range:u64,
 pub residual_count:u32,pub guest_count:u32,pub residual_at:[u32;4],pub guest_at:[u32;4]}
#[repr(C)]
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub struct GenericBufferTemplates {pub version:u32,pub count:u32,pub max_vertex_alignment:u32,pub reserved:u32,
 pub slots:[GenericBufferTemplate;16]}
impl Default for GenericBufferTemplates {fn default()->Self{Self{version:0,count:0,max_vertex_alignment:0,reserved:0,
 slots:[GenericBufferTemplate::default();16]}}}
const _:()=assert!(size_of::<GenericBufferTemplate>()==88&&size_of::<GenericBufferTemplates>()==1424);

fn generic_buffer_templates(handle:&RenderPipelineHandle,resources:&[RenderResource],layouts:&[VertexLayout],
 d:&RenderDraw,given:&GenericPlan)->Result<GenericBufferTemplates,String>{
 use metal2vulkan::{bind::{BoundBuffer,FieldLocation},pipeline::{PushField,Stage,Window}};
 let (fresh,_)=generic_plan_mode(handle,resources,layouts,d,true)?;
 if fresh!=*given{return Err("buffer templates: the given plan is not this draw's plan".into());}
 /* The state and plan generic_plan_mode instantiated (KEEP IN SYNC with its
  * prefix; it has validated every input above). */
 let mut state=compositor_plan_state(resources,true)?;
 let vertex_end=(d.vertex_start+d.vertex_count) as u64;let instance_end=(d.base_instance+d.instance_count) as u64;
 for layout in layouts {
  let count=if layout.step_function==1{vertex_end}else{instance_end};
  let vertex_stage=state.stages.get_mut(&Stage::Vertex).ok_or("generic vertex stage missing")?;
  vertex_stage.buffers.insert(layout.index,BoundBuffer::Buffer{length:count*layout.stride as u64,offset:0,gpu_address:0,
   device_address:None,attribute_stride:Some(layout.stride)});
 }
 let mut plan_ref=handle.cached_plan(&state)?;
 if !plan_ref.variant.is_empty(){let v=handle.variant_for(&plan_ref.variant)?;plan_ref=v.plan(&handle._device,&state)?;}
 let plan=&*plan_ref;
 let stage_id=|s:Stage|match s {Stage::Vertex=>Ok(1u32),Stage::Fragment=>Ok(2u32),_=>Err("buffer template stage unsupported".to_owned())};
 let stage_bit=|s:Stage|match s {Stage::Vertex=>1u32,Stage::Fragment=>0x10u32,_=>0};
 let slots=plan.buffer_slot_templates();
 if slots.iter().any(|t|t.written){return Err("buffer templates: the plan writes a buffer slot".into());}
 let mut out=GenericBufferTemplates::default();
 out.version=1;out.max_vertex_alignment=metal2vulkan::metal::vertex::MAX_VERTEX_ALIGNMENT;
 for t in &slots {
  let stage=stage_id(t.slot.stage)?;
  let Some(r)=resources.iter().find(|r|r.kind==1&&r.stage==stage&&r.index==t.slot.index) else{continue};
  if stage==1&&layouts.iter().any(|l|l.index==t.slot.index){return Err("buffer templates: a vertex layout slot is planned".into());}
  let Some((0,binding))=t.descriptor else{return Err("buffer templates: a slot is not a set-0 descriptor".into())};
  let (offset,range,residual)=t.descriptor_range(r.offset,r.length).ok_or("buffer templates: range refused")?;
  let mut e=GenericBufferTemplate{stage,index:t.slot.index,binding,alignment:t.alignment,max_range:t.max_range,..Default::default()};
  match t.window {Window::Bounded{start,len}=>{e.window_kind=1;e.window_start=start;e.window_len=len;},Window::Whole=>e.window_kind=2,
   _=>return Err("buffer templates: window form unsupported".into())}
  let mut covered=0;
  for b in &given.bindings[..given.binding_count as usize] {
   if b.kind!=GENERIC_BUFFER||b.binding!=binding{continue;}
   if b.stage!=stage||b.index!=t.slot.index||b.bind_offset!=r.offset||b.resource_length!=r.length||
      b.descriptor_offset!=offset||b.descriptor_length!=range{return Err("buffer templates: a binding is not restated".into());}
   covered+=1;
  }
  if covered==0{return Err("buffer templates: a planned slot has no binding".into());}
  for f in &plan.push.fields {
   let guest=match f.field {PushField::SlotBindOffset(s) if s==t.slot=>false,PushField::SlotGuestBase(s) if s==t.slot=>true,_=>continue};
   let FieldLocation::PushWindow{offset:o}=f.location else{return Err("buffer templates: a slot field is spilled".into())};
   let want:Vec<u8>=if guest{r.offset.to_le_bytes().to_vec()}else{residual.to_le_bytes().to_vec()};
   if f.size as usize!=want.len(){return Err("buffer templates: a slot field has another size".into());}
   let bit=stage_bit(f.stage);let mut placed=0;
   for s in &given.pushes[..given.push_count as usize] {
    if s.stage&bit==0||o<s.offset||o as u64+f.size as u64>s.offset as u64+s.length as u64{continue;}
    let at=(s.bytes_offset+(o-s.offset)) as usize;
    if given.push_bytes.get(at..at+want.len())!=Some(&want[..]){return Err("buffer templates: push bytes are not restated".into());}
    let (n,list)=if guest{(&mut e.guest_count,&mut e.guest_at)}else{(&mut e.residual_count,&mut e.residual_at)};
    if *n as usize>=list.len(){return Err("buffer templates: too many field copies".into());}
    list[*n as usize]=at as u32;*n+=1;placed+=1;
   }
   if placed==0{return Err("buffer templates: a slot field is outside the push spans".into());}
  }
  if out.count as usize>=out.slots.len(){return Err("buffer templates: too many slots".into());}
  out.slots[out.count as usize]=e;out.count+=1;
 }
 for b in &given.bindings[..given.binding_count as usize] {
  if b.kind==GENERIC_BUFFER&&!out.slots[..out.count as usize].iter().any(|t|t.binding==b.binding){
   return Err("buffer templates: a buffer binding has no template".into());}
 }
 Ok(out)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_plan_generic_buffer_templates(
 pipeline:*const RenderPipelineHandle,resources:*const RenderResource,count:usize,
 layouts:*const VertexLayout,layout_count:usize,draw:*const RenderDraw,given:*const GenericPlan,
 out:*mut GenericBufferTemplates,error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe{error_copy(error,cap,"null buffer template output")};return -1;}
 let out_addr=out as usize;let err_addr=error as usize;
 let input_spans=[(resources as usize,if resources.is_null(){0}else{count.saturating_mul(size_of::<RenderResource>())}),
  (layouts as usize,if layouts.is_null(){0}else{layout_count.saturating_mul(size_of::<VertexLayout>())}),
  (draw as usize,if draw.is_null(){0}else{size_of::<RenderDraw>()}),
  (given as usize,if given.is_null(){0}else{size_of::<GenericPlan>()}),
  (pipeline as usize,if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()})];
 if compositor_ranges_overlap(out_addr,size_of::<GenericBufferTemplates>(),err_addr,cap)||
    input_spans.iter().any(|(p,n)|compositor_ranges_overlap(out_addr,size_of::<GenericBufferTemplates>(),*p,*n)||
      compositor_ranges_overlap(err_addr,cap,*p,*n)){return -1;}
 unsafe{*out=GenericBufferTemplates::default();}
 match unsafe{attempt(error,cap,||{
  if pipeline.is_null()||draw.is_null()||given.is_null()||(count!=0&&resources.is_null())||count>186||
     (layout_count!=0&&layouts.is_null())||layout_count>8{return Err("invalid buffer template inputs".into());}
  let resources=if count==0{&[][..]}else{slice::from_raw_parts(resources,count)};
  let layouts=if layout_count==0{&[][..]}else{slice::from_raw_parts(layouts,layout_count)};
  generic_buffer_templates(&*pipeline,resources,layouts,&*draw,&*given)
 })}{Some(value)=>{unsafe{*out=value};0},None=>-1}
}

/// Pure metadata-only RenderPipeline::plan + BindingPlan::instantiate_draw for
/// a bounded typed render draw. No native approval or execution.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_plan_draw(
 pipeline:*const RenderPipelineHandle,resources:*const RenderResource,count:usize,draw:*const RenderDraw,
 out:*mut Blob,error:*mut c_char,cap:usize)->i32{
 let oi=out as usize;let ei=error as usize;let nresources=count.saturating_mul(size_of::<RenderResource>());
 let input_spans=[(resources as usize,if resources.is_null(){0}else{nresources}),
  (draw as usize,if draw.is_null(){0}else{size_of::<RenderDraw>()}),
  (pipeline as usize,if pipeline.is_null(){0}else{size_of::<RenderPipelineHandle>()})];
 let error_aliases_input=ei!=0&&input_spans.iter().any(|(p,n)|compositor_ranges_overlap(ei,cap,*p,*n));
 if out.is_null(){if !error_aliases_input{unsafe{error_copy(error,cap,"null compositor plan output")};}return -1;}
 let output_aliases_input=input_spans.iter().any(|(p,n)|compositor_ranges_overlap(oi,size_of::<Blob>(),*p,*n));
 if compositor_ranges_overlap(oi,size_of::<Blob>(),ei,cap)||output_aliases_input||error_aliases_input{return -1;}
 unsafe{*out=Blob{bytes:ptr::null_mut(),length:0};}
 match unsafe{attempt(error,cap,||{
  if pipeline.is_null()||draw.is_null()||(count>0&&resources.is_null())||count>186{return Err("invalid compositor plan inputs".into());}
  let handle=&*pipeline;
  if handle.requested_attachments.is_empty()||handle.requested_attachments.len()>8{return Err("compositor plan requires an explicit bounded color target list".into());}
  if handle._pipeline.graphics.color_attachments.len()!=handle.requested_attachments.len(){return Err("compiled attachment topology differs from requested target list".into());}
  let in_slice=if count==0 {&[]} else {slice::from_raw_parts(resources,count)};let d=&*draw;
  if d.version!=1||d.vertex_count==0||d.vertex_count>(1<<24)||d.instance_count==0||d.instance_count>(1<<20)||
     d.vertex_start.checked_add(d.vertex_count).is_none()||d.base_instance.checked_add(d.instance_count).is_none(){return Err("invalid or unbounded compositor direct draw range".into());}
  let (state,bound)=compositor_plan_inputs(in_slice)?;
  let plan_ref=handle.cached_plan(&state)?;let plan=&*plan_ref;
  if !plan.variant.is_empty(){return Err(format!("unhandled compositor plan variant: {:?}",plan.variant));}
  let draw_call=metal2vulkan::bind::DrawCall::Primitives{vertex_start:d.vertex_start,vertex_count:d.vertex_count,instance_count:d.instance_count,base_instance:d.base_instance};
  let commands=plan.instantiate_draw(&state,&draw_call).map_err(|e|e.to_string())?;
  let p=&handle._pipeline;let req=&p.requirements;
  let aux=serde_json::json!({"api_version":req.api_version.map(|v|format!("{v:?}")),"spirv_capabilities":req.spirv_capabilities.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),"spirv_extensions":req.spirv_extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),"features":req.features.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),"extensions":req.extensions.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),"properties":req.properties.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),"limits":req.limits.iter().map(|(k,v)|(format!("{k:?}"),*v)).collect::<std::collections::BTreeMap<_,_>>(),"formats":req.formats.iter().map(|(f,u)|(format!("{f:?}"),format!("{u:?}"))).collect::<Vec<_>>(),"stage_flags":req.stage_flags.iter().map(|v|format!("{v:?}")).collect::<Vec<_>>(),"denorm_independence":format!("{:?}",req.denorm_independence),"rounding_independence":format!("{:?}",req.rounding_independence),"pipeline_interface":p.interface,"graphics_state":p.graphics,"dependencies":p.dependencies,"plan_variant":plan.variant,"plan_diagnostics":plan.diagnostics,"plan_uses":plan.uses});
  let attachment_json=handle.requested_attachments.iter().map(|a|serde_json::json!({"format":a.format,"write_mask":a.write_mask})).collect::<Vec<_>>();
  let draw_json=serde_json::json!({"version":d.version,"vertex_start":d.vertex_start,"vertex_count":d.vertex_count,"instance_count":d.instance_count,"base_instance":d.base_instance});
  let json=serde_json::to_vec(&serde_json::json!({"schema":1,"scope":"metadata-only compositor binding plan; not native execution approval","attachments":attachment_json,"bound_resources":bound,"draw":draw_json,"plan":plan,"commands":commands,"auxiliary_requirements":aux})).map_err(|e|e.to_string())?;
  Ok(json)
 })}{Some(json)=>{unsafe{*out=blob(json)};0},None=>-1}
}

/// Wire layout matches C tegu_blend_inputs. Fixed-function policy is also
/// checked by the shared C packer before kernel import; neither check submits.
#[repr(C)]
#[derive(Clone,Copy,PartialEq,Eq)]
pub struct RenderBlend {
 pub version:u32,pub enabled:u32,pub rgb_op:u32,pub rgb_src:u32,pub rgb_dst:u32,
 pub alpha_op:u32,pub alpha_src:u32,pub alpha_dst:u32,pub write_mask:u32,
}
impl RenderBlend {
 fn replaces(&self)->bool {
  self.enabled==0||(self.rgb_src==1&&self.rgb_dst==0&&self.rgb_op==0&&
   self.alpha_src==1&&self.alpha_dst==0&&self.alpha_op==0)
 }
 fn channel(op:u32,src:u32,dst:u32,alpha:bool)->bool {
  let channel=|f:u32|if alpha {match f {2=>4,3=>5,6=>8,7=>9,_=>f}} else {f};
  let (src,dst)=(channel(src),channel(dst));
  // Blend colour factors 11..14 are one Mali operand (the scalar BLEND
  // constant, blend_state.c BF_CONSTANT); 10 (SourceAlphaSaturated) is refused.
  let base=|f:u32|if f<2 {0} else if f>=11 {11} else {f&!1};
  base(src)==base(dst)||base(src)==0||base(dst)==0||
   op==0&&((src==6&&dst==2)||(alpha&&src==8&&dst==4))
 }
 fn apply(&self,color:&mut metal2vulkan::pipeline::ColorAttachmentDescriptor)->Result<(),String> {
  use metal2vulkan::metal::{BlendFactor,BlendOperation,PixelFormat};
  if self.version!=1||self.enabled>1||self.write_mask>15||self.rgb_op>2||self.alpha_op>2||
     [self.rgb_src,self.rgb_dst,self.alpha_src,self.alpha_dst].iter().any(|v|*v==10||*v>14) {
   return Err("native blend descriptor outside bounded enums/mask".into());
  }
  if self.enabled!=0&&(!matches!(color.pixel_format,PixelFormat::R8Unorm|PixelFormat::RGBA8Unorm|PixelFormat::BGRA8Unorm|
     PixelFormat::BGR5A1Unorm|PixelFormat::RGBA16Float|PixelFormat::R16Float|PixelFormat::RG16Float|
     PixelFormat::RGB10A2Unorm|PixelFormat::BGR10A2Unorm|PixelFormat::B5G6R5Unorm|PixelFormat::ABGR4Unorm|
     PixelFormat::RGBA8UnormSRGB|PixelFormat::BGRA8UnormSRGB|PixelFormat::RG11B10Float|PixelFormat::A8Unorm|
     PixelFormat::R8UnormSRGB|PixelFormat::RG8Unorm|PixelFormat::RG8UnormSRGB)||
     !Self::channel(self.rgb_op,self.rgb_src,self.rgb_dst,false)||
     !Self::channel(self.alpha_op,self.alpha_src,self.alpha_dst,true)) {
   return Err("native blend requires supported fixed-function equations".into());
  }
  color.write_mask=metal2vulkan::metal::ColorWriteMask::try_from(self.write_mask).map_err(|e|e.to_string())?;
  color.blending_enabled=self.enabled!=0;
  color.rgb_blend_operation=BlendOperation::try_from(self.rgb_op).map_err(|e|e.to_string())?;
  color.alpha_blend_operation=BlendOperation::try_from(self.alpha_op).map_err(|e|e.to_string())?;
  color.source_rgb_blend_factor=BlendFactor::try_from(self.rgb_src).map_err(|e|e.to_string())?;
  color.destination_rgb_blend_factor=BlendFactor::try_from(self.rgb_dst).map_err(|e|e.to_string())?;
  color.source_alpha_blend_factor=BlendFactor::try_from(self.alpha_src).map_err(|e|e.to_string())?;
  color.destination_alpha_blend_factor=BlendFactor::try_from(self.alpha_dst).map_err(|e|e.to_string())?;
  Ok(())
 }
}
/// Verify the compiled Vulkan blend state against the requested Metal state,
/// then keep every other native graphics/resource/stage/capability gate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_native_check_with_blend(
 pipeline:*const RenderPipelineHandle,format:u32,blend:*const RenderBlend,
 error:*mut c_char,cap:usize)->i32 {
 match unsafe {attempt(error,cap,|| {
  use metal2vulkan::{metal::PixelFormat,pipeline::ColorAttachmentDescriptor};
  if pipeline.is_null()||blend.is_null(){return Err("null render pipeline or blend descriptor".into());}
  let mut color=ColorAttachmentDescriptor::new(match format {
   70=>PixelFormat::RGBA8Unorm,80=>PixelFormat::BGRA8Unorm,_=>return Err("unsupported native color format".into())});
  (*blend).apply(&mut color)?;
  if (*pipeline).source_blend.is_some_and(|original|original!=*blend)||
     ((*pipeline).source_blend.is_none()&&(*blend).enabled!=0) {
   return Err("native render blend descriptor differs from compiled input".into());
  }
  // The translator elides replacement blending on UNORM attachments. This
  // exact One/Zero ADD case also produces the C packer's opaque bytes.
  let expected=if (*blend).replaces(){None}else{
   metal2vulkan::metal::state::blend_state(&color,(*pipeline)._device.profile()).map_err(|e|e.to_string())?};
  native_render_check(&(*pipeline)._pipeline,format,true,expected)
 })} {Some(())=>0,None=>-1}
}

/// AIR push bytes only, before the Mali backend's FAU base adjustment. This is
/// deliberately not a certificate that the pipeline can execute on the GPU.
#[repr(C)]
pub struct RenderPush {
    pub version: u32,
    pub vertex_span: u32,
    pub fragment_span: u32,
    pub vertex: [u8; 128],
    pub fragment: [u8; 128],
}
fn render_push(placements: &[metal2vulkan::pipeline::PushPlacement], rules: &[(u32,u32)]) -> Result<RenderPush,String> {
    use metal2vulkan::pipeline::{PushField,PushLoc};
    let mut out=RenderPush{version:1,vertex_span:0,fragment_span:0,vertex:[0;128],fragment:[0;128]};
    let mut stages=0u32;
    for placement in placements {
        let (bytes,span,bit)=match placement.stage {
            Stage::Vertex=>(&mut out.vertex,&mut out.vertex_span,1),
            Stage::Fragment=>(&mut out.fragment,&mut out.fragment_span,2),
            _=>return Err("unsupported render push stage".into()),
        };
        if stages & bit != 0 {return Err("duplicate render push stage".into());}
        stages |= bit;
        if placement.argument_table_size!=0 {return Err("render argument-table push unsupported".into());}
        let (start,end)=match placement.push_range {
            Some((offset,size))=>{
                let end=offset.checked_add(size).ok_or("render push range overflow")?;
                if size==0 || end>128 {return Err("render push range outside native span".into());}
                (offset,end)
            },
            None=>(0,0),
        };
        *span=end;
        let mut written=[false;128];
        for field in &placement.fields {
            let offset=match field.loc {
                PushLoc::Push{offset}=>offset,
                _=>return Err("render push location unsupported".into()),
            };
            let limit=offset.checked_add(field.size).ok_or("render push field overflow")?;
            if field.size!=4 || offset%4!=0 || offset<start || limit>end {
                return Err("render push field outside declared range".into());
            }
            let value=match field.field {
                PushField::ColorWriteRule(color) if placement.stage==Stage::Fragment=>{
                    let mut rules=rules.iter().filter(|(c,_)|*c==color);
                    let value=rules.next().ok_or("missing render color write rule")?.1;
                    if rules.next().is_some() {return Err("duplicate render color write rule".into());}
                    value
                },
                _=>return Err("render push field requires unsupported draw binding".into()),
            };
            let range=offset as usize..limit as usize;
            if written[range.clone()].iter().any(|v|*v) {return Err("overlapping render push fields".into());}
            written[range.clone()].fill(true);
            bytes[range].copy_from_slice(&value.to_le_bytes());
        }
    }
    Ok(out)
}
/// Resolves pipeline-static render constants, refusing fields requiring a draw
/// binding. Output is zero on failure; valid handles may outlive their library.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_push(
 pipeline:*const RenderPipelineHandle,out:*mut RenderPush,error:*mut c_char,cap:usize)->i32 {
    if out.is_null() {unsafe {error_copy(error,cap,"null render push output")};return -1;}
    unsafe {ptr::write_bytes(out,0,1);}
    match unsafe {attempt(error,cap,|| {
        if pipeline.is_null() {return Err("null render pipeline".into());}
        let pipeline=&(*pipeline)._pipeline;
        render_push(&pipeline.interface.push,&pipeline.graphics.color_write_rules)
    })} {
        Some(value)=>{unsafe {*out=value};0},
        None=>-1,
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_close(pipeline:*mut RenderPipelineHandle) {
    if !pipeline.is_null() {drop(unsafe {Box::from_raw(pipeline)});}
}

/* AIR-planned render pipeline (no translation): the planner metadata comes from
 * the AIR compiler's reflection of the two functions (libNord2Mali
 * tegu_mali_air_render_metadata, metal2vulkan schema-1 JSON: layout, interface,
 * dependencies, stages[].specialization, vertex_input, graphics, variant_bits) and
 * becomes a metal2vulkan RenderPipeline through RenderPipeline::from_parts with
 * StageModule::external stages, so every planner entry (plan_generic*, buffer
 * templates, push, vertex_fetch, serialize/deserialize, packs) works unchanged.
 * Same descriptor inputs and checks as tegu_air_render_pipeline_create_with_*
 * (color_format+blend, or targets+attachment_blends). The returned SPIR-V blobs
 * are interface-facts modules (air_facts_module) and the metadata blob is
 * render_outputs' (identical after a cache round trip). No
 * translator variant is ever compiled for it (variant_for refuses). */
/* Interface-facts module of an AIR-planned stage: a SPIR-V 1.5 header whose
 * generator word is "TGAP" and, no code, only what the frontend's module
 * readers take from a stage module: BuiltIn VertexIndex (render_pipeline.inc
 * tegu_render_spirv_uses_vertex_index), a SubpassData image and its
 * InputAttachmentIndex per [[color(n)]] read (tegu_render_spirv_declares_subpass),
 * and the stage's specialization constants as 32-bit OpSpecConstants with their
 * SpecIds and defaults (tegu_spec_push_scan: the runtime stand-in's push layout).
 * Every id is defined by an instruction of its own (cache/scan.rs walk). */
const AIR_PLAN_GENERATOR:u32=0x5447_4150;
fn air_facts_module(vertex_index:bool,input_attachments:u32,spec:&[(u32,u32)])->Vec<u32> {
 let mut w=vec![0x0723_0203,0x0001_0500,AIR_PLAN_GENERATOR,1,0];
 let mut next=1u32;
 if vertex_index {let id=next;next+=1;w.extend_from_slice(&[(4<<16)|71,id,11,42]);}
 if input_attachments!=0 {
  let id=next;next+=1;
  w.extend_from_slice(&[(9<<16)|25,id,id,6,0,0,0,2,0]);
  for n in 0..8 {if input_attachments&(1<<n)!=0 {w.extend_from_slice(&[(4<<16)|71,id,43,n]);}}
 }
 if !spec.is_empty() {
  let ty=next;next+=1;
  w.extend_from_slice(&[(4<<16)|21,ty,32,0]);
  for &(spec_id,default) in spec {
   let id=next;next+=1;
   w.extend_from_slice(&[(4<<16)|50,ty,id,default]);
   w.extend_from_slice(&[(4<<16)|71,id,1,spec_id]);
  }
 }
 w[3]=next;
 w
}
fn air_plan_key(parts:&[&[u8]])->[u8;32] {
 let mut out=[0u8;32];
 for (lane,chunk) in out.chunks_mut(8).enumerate() {
  let mut h:u64=0xcbf29ce484222325^((lane as u64+1).wrapping_mul(0x9e3779b97f4a7c15));
  for p in parts {
   for b in (p.len() as u64).to_le_bytes().iter().chain(p.iter()) {h^=u64::from(*b);h=h.wrapping_mul(0x100000001b3);}
  }
  chunk.copy_from_slice(&h.to_le_bytes());
 }
 out
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_create_planned(
 vertex_library:*const Library,vertex_name:*const u8,vertex_name_len:usize,
 fragment_library:*const Library,fragment_name:*const u8,fragment_name_len:usize,
 color_format:u32,attributes:*const VertexAttribute,attribute_count:usize,
 layouts:*const VertexLayout,layout_count:usize,blend:*const RenderBlend,
 targets:*const RenderColorAttachment,target_count:usize,attachment_blends:*const RenderBlend,
 air_metadata:*const u8,air_metadata_len:usize,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 use metal2vulkan::pipeline::{RenderPipeline,StageModule,PipelineLayout,PipelineInterface,GraphicsState,VertexInputState,
  RuntimeDependency,SpecializationLayout};
 if vertex_spirv.is_null()||fragment_spirv.is_null()||metadata.is_null()||
    vertex_spirv==fragment_spirv||vertex_spirv==metadata||fragment_spirv==metadata {
  unsafe {error_copy(error,cap,"invalid or aliased render outputs")};return ptr::null_mut();
 }
 unsafe {
  *vertex_spirv=Blob{bytes:ptr::null_mut(),length:0};*fragment_spirv=Blob{bytes:ptr::null_mut(),length:0};
  *metadata=Blob{bytes:ptr::null_mut(),length:0};
 }
 let result=unsafe {attempt(error,cap,|| {
  if target_count>0&&!blend.is_null(){return Err("explicit attachment prototype does not combine with blend descriptor".into());}
  let (_descriptor,attachment_snapshot)=render_descriptor_from_inputs(vertex_library,vertex_name,vertex_name_len,
   fragment_library,fragment_name,fragment_name_len,color_format,attributes,attribute_count,layouts,layout_count,
   blend,false,targets,target_count,attachment_blends)?;
  let meta_bytes=input(air_metadata,air_metadata_len,16*1024*1024)?;
  let json=str::from_utf8(input(profile,profile_len,1024*1024)?).map_err(|e|e.to_string())?;
  let device=device_for_profile(json)?;
  let v:serde_json::Value=serde_json::from_slice(meta_bytes).map_err(|e|format!("AIR plan metadata: {e}"))?;
  if v.get("schema").and_then(|x|x.as_u64())!=Some(1)||v.get("source").and_then(|x|x.as_str())!=Some("air") {
   return Err("AIR plan metadata: schema 1 from the AIR reflection expected".into());
  }
  macro_rules! part {($v:expr,$key:expr,$t:ty)=>{
   serde_json::from_value::<$t>($v.get($key).cloned().ok_or_else(||format!("AIR plan metadata lacks {}",$key))?)
    .map_err(|e|format!("AIR plan metadata {}: {e}",$key))?
  }}
  let layout=part!(v,"layout",PipelineLayout);
  let interface=part!(v,"interface",PipelineInterface);
  let graphics=part!(v,"graphics",GraphicsState);
  let vertex_input=part!(v,"vertex_input",VertexInputState);
  let dependencies=part!(v,"dependencies",Vec<RuntimeDependency>);
  let variant_bits=part!(v,"variant_bits",u32);
  let stages=v.get("stages").and_then(|x|x.as_array()).filter(|a|a.len()==2).ok_or("AIR plan metadata: two stages expected")?;
  let facts=v.get("air_facts").ok_or("AIR plan metadata lacks air_facts")?;
  let vertex_index=facts.get("vertex_index").and_then(|x|x.as_bool()).ok_or("AIR plan metadata: air_facts.vertex_index")?;
  let input_attachments=facts.get("input_attachments").and_then(|x|x.as_u64()).filter(|m|*m<256).ok_or("AIR plan metadata: air_facts.input_attachments")? as u32;
  let mut modules=Vec::with_capacity(2);
  for (i,(st,name,len)) in [(vk::ShaderStage::VERTEX,vertex_name,vertex_name_len),(vk::ShaderStage::FRAGMENT,fragment_name,fragment_name_len)].into_iter().enumerate() {
   let spec=part!(stages[i],"specialization",SpecializationLayout);
   if stages[i].get("stage").and_then(|x|x.as_u64())!=Some(u64::from(st.as_raw())){return Err("AIR plan metadata: stage order".into());}
   let entry=str::from_utf8(input(name,len,4096)?).map_err(|e|e.to_string())?.to_owned();
   if spec.constants.iter().any(|c|c.size!=4) {return Err("AIR plan metadata: a specialization constant is not 32-bit".into());}
   let constants:Vec<(u32,u32)>=spec.constants.iter().map(|c|(c.id,c.default as u32)).collect();
   let words=if i==0 {air_facts_module(vertex_index,0,&constants)} else {air_facts_module(false,input_attachments,&constants)};
   modules.push(StageModule::external(st,entry,spec,std::sync::Arc::from(words)));
  }
  let mut desc=Vec::new();
  desc.extend_from_slice(&color_format.to_le_bytes());
  for a in &attachment_snapshot {desc.extend_from_slice(&a.format.to_le_bytes());desc.extend_from_slice(&a.write_mask.to_le_bytes());}
  let profile_fp=device.profile_fingerprint();
  let key=air_plan_key(&[b"TGAP1",meta_bytes,&desc,&profile_fp[..]]);
  let pipeline=RenderPipeline::from_parts(metal2vulkan::cache::PipelineKey::new(key),profile_fp,modules,layout,vertex_input,
   graphics,dependencies,interface,variant_bits);
  let handle=RenderPipelineHandle{_device:device,_pipeline:pipeline,source_blend:if blend.is_null(){None}else{Some(*blend)},
   requested_attachments:attachment_snapshot,
   requested_attachment_blends:if attachment_blends.is_null(){None}else{Some((0..target_count).map(|i|*attachment_blends.add(i)).collect())},
   color_format,explicit_targets:target_count>0,plan_cache:Default::default(),
   descriptor:std::sync::Mutex::new(None),variants:Default::default()};
  /* exactly render_outputs, as a deserialized handle gives (the Mali code cache keys the metadata) */
  let (vs,fs,encoded)=render_outputs(&handle)?;
  Ok((vs,fs,encoded,handle))
 })};
 match result {
  Some((v,f,m,handle))=>{unsafe {*vertex_spirv=blob(v);*fragment_spirv=blob(f);*metadata=blob(m)};Box::into_raw(Box::new(handle))},
  None=>ptr::null_mut(),
 }
}

/* Pipeline cache support: the frontend stores these bytes under its own key
 * (library identities, names, descriptor, profile, build) inside a
 * checksummed envelope.  Layout (little endian): "TGRH", u32 version,
 * u32 color_format, u8 explicit_targets, u8 has_source_blend, 9xu32 blend,
 * u32 n + n x (format,mask), u8 has_blends, u32 n + n x 9xu32, key[32],
 * u64 len + metal2vulkan RenderPipeline::to_bytes (its own versions,
 * profile fingerprint and checksum). */
const RENDER_HANDLE_MAGIC:&[u8;4]=b"TGRH";
const RENDER_HANDLE_VERSION:u32=2; /* 2: compiled translator variants follow the base */
fn put_blend(out:&mut Vec<u8>,b:&RenderBlend) {
 for v in [b.version,b.enabled,b.rgb_op,b.rgb_src,b.rgb_dst,b.alpha_op,b.alpha_src,b.alpha_dst,b.write_mask] {out.extend_from_slice(&v.to_le_bytes());}
}
fn serialize_render_handle(h:&RenderPipelineHandle)->Vec<u8> {
 use metal2vulkan::cache::PipelineArtifact;
 let mut out=Vec::new();
 out.extend_from_slice(RENDER_HANDLE_MAGIC);out.extend_from_slice(&RENDER_HANDLE_VERSION.to_le_bytes());
 out.extend_from_slice(&h.color_format.to_le_bytes());out.push(h.explicit_targets as u8);
 out.push(h.source_blend.is_some() as u8);
 put_blend(&mut out,&h.source_blend.unwrap_or(RenderBlend{version:0,enabled:0,rgb_op:0,rgb_src:0,rgb_dst:0,alpha_op:0,alpha_src:0,alpha_dst:0,write_mask:0}));
 out.extend_from_slice(&(h.requested_attachments.len() as u32).to_le_bytes());
 for a in &h.requested_attachments {out.extend_from_slice(&a.format.to_le_bytes());out.extend_from_slice(&a.write_mask.to_le_bytes());}
 out.push(h.requested_attachment_blends.is_some() as u8);
 let blends=h.requested_attachment_blends.as_deref().unwrap_or(&[]);
 out.extend_from_slice(&(blends.len() as u32).to_le_bytes());
 for b in blends {put_blend(&mut out,b);}
 out.extend_from_slice(h._pipeline.key.digest());
 let body=h._pipeline.to_bytes();
 out.extend_from_slice(&(body.len() as u64).to_le_bytes());out.extend_from_slice(&body);
 let variants=h.variants.lock().unwrap_or_else(|e|e.into_inner()).clone();
 out.extend_from_slice(&(variants.len() as u32).to_le_bytes());
 for v in variants {
  let key=serde_json::to_vec(&v.key).unwrap_or_default();
  out.extend_from_slice(&(key.len() as u32).to_le_bytes());out.extend_from_slice(&key);
  out.extend_from_slice(v.pipeline.key.digest());
  let body=v.pipeline.to_bytes();
  out.extend_from_slice(&(body.len() as u64).to_le_bytes());out.extend_from_slice(&body);
 }
 out
}
struct HandleReader<'a>{bytes:&'a [u8],at:usize}
impl<'a> HandleReader<'a> {
 fn take(&mut self,n:usize)->Result<&'a [u8],String> {
  let end=self.at.checked_add(n).filter(|e|*e<=self.bytes.len()).ok_or("cached render pipeline truncated")?;
  let v=&self.bytes[self.at..end];self.at=end;Ok(v)
 }
 fn u8(&mut self)->Result<u8,String>{Ok(self.take(1)?[0])}
 fn u32(&mut self)->Result<u32,String>{Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))}
 fn u64(&mut self)->Result<u64,String>{Ok(u64::from_le_bytes(self.take(8)?.try_into().unwrap()))}
 fn flag(&mut self)->Result<bool,String>{match self.u8()? {0=>Ok(false),1=>Ok(true),_=>Err("cached render pipeline flag invalid".into())}}
 fn blend(&mut self)->Result<RenderBlend,String>{
  Ok(RenderBlend{version:self.u32()?,enabled:self.u32()?,rgb_op:self.u32()?,rgb_src:self.u32()?,rgb_dst:self.u32()?,
   alpha_op:self.u32()?,alpha_src:self.u32()?,alpha_dst:self.u32()?,write_mask:self.u32()?})
 }
}
fn deserialize_render_handle(bytes:&[u8],profile:&str)->Result<RenderPipelineHandle,String> {
 use metal2vulkan::cache::{PipelineArtifact,PipelineKey};
 let mut r=HandleReader{bytes,at:0};
 if r.take(4)?!=RENDER_HANDLE_MAGIC||r.u32()?!=RENDER_HANDLE_VERSION {return Err("cached render pipeline version differs".into());}
 let color_format=r.u32()?;let explicit_targets=r.flag()?;
 let has_source=r.flag()?;let source=r.blend()?;
 let n=r.u32()? as usize;if n>8 {return Err("cached render pipeline attachments invalid".into());}
 let mut requested_attachments=Vec::with_capacity(n);
 for _ in 0..n {requested_attachments.push(RenderColorAttachment{format:r.u32()?,write_mask:r.u32()?});}
 let has_blends=r.flag()?;let m=r.u32()? as usize;
 if m>8||(!has_blends&&m!=0) {return Err("cached render pipeline blends invalid".into());}
 let mut blends=Vec::with_capacity(m);for _ in 0..m {blends.push(r.blend()?);}
 let key:[u8;32]=r.take(32)?.try_into().unwrap();
 let len=usize::try_from(r.u64()?).map_err(|_|"cached render pipeline length invalid")?;
 let body=r.take(len)?;
 let device=device_for_profile(profile)?;
 let pipeline=metal2vulkan::pipeline::RenderPipeline::from_bytes_for_key(body,&PipelineKey::new(key),&device).map_err(|e|format!("cached render pipeline rejected: {e:?}"))?;
 let count=r.u32()? as usize;if count>RENDER_VARIANT_LIMIT {return Err("cached render pipeline variants invalid".into());}
 let mut variants=Vec::with_capacity(count);
 for _ in 0..count {
  let klen=r.u32()? as usize;if klen>1<<20 {return Err("cached render variant key invalid".into());}
  let vkey:VariantKey=serde_json::from_slice(r.take(klen)?).map_err(|e|format!("cached render variant key: {e}"))?;
  let digest:[u8;32]=r.take(32)?.try_into().unwrap();
  let blen=usize::try_from(r.u64()?).map_err(|_|"cached render variant length invalid")?;
  let vbody=r.take(blen)?;
  let vp=metal2vulkan::pipeline::RenderPipeline::from_bytes_for_key(vbody,&PipelineKey::new(digest),&device).map_err(|e|format!("cached render variant rejected: {e:?}"))?;
  variants.push(std::sync::Arc::new(VariantPipeline{hash:variant_key_hash(&vkey),key:vkey,pipeline:vp,plan_cache:Default::default()}));
 }
 if r.at!=bytes.len() {return Err("cached render pipeline has trailing bytes".into());}
 Ok(RenderPipelineHandle{_device:device,_pipeline:pipeline,source_blend:if has_source{Some(source)}else{None},
  requested_attachments,requested_attachment_blends:if has_blends{Some(blends)}else{None},color_format,explicit_targets,plan_cache:Default::default(),
  descriptor:std::sync::Mutex::new(None),variants:std::sync::Mutex::new(variants)})
}
/// Bytes that rebuild this pipeline (handle, SPIR-V and metadata blobs) with
/// tegu_air_render_pipeline_deserialize under the same profile.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_serialize(pipeline:*const RenderPipelineHandle,out:*mut Blob,
 error:*mut c_char,cap:usize)->i32 {
 if out.is_null(){unsafe {error_copy(error,cap,"null serialize output")};return -1;}
 unsafe {*out=Blob{bytes:ptr::null_mut(),length:0}};
 match unsafe {attempt(error,cap,|| {
  if pipeline.is_null(){return Err("null render pipeline".into());}
  Ok(serialize_render_handle(&*pipeline))
 })} {Some(bytes)=>{unsafe {*out=blob(bytes)};0},None=>-1}
}
/// Rebuild a serialized pipeline and the three blobs its creation returned.
/// Any error is a cache miss for the caller.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_render_pipeline_deserialize(bytes:*const u8,len:usize,profile:*const u8,profile_len:usize,
 vertex_spirv:*mut Blob,fragment_spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut RenderPipelineHandle {
 if vertex_spirv.is_null()||fragment_spirv.is_null()||metadata.is_null()||
    vertex_spirv==fragment_spirv||vertex_spirv==metadata||fragment_spirv==metadata {
  unsafe {error_copy(error,cap,"invalid or aliased render outputs")};return ptr::null_mut();
 }
 unsafe {
  *vertex_spirv=Blob{bytes:ptr::null_mut(),length:0};*fragment_spirv=Blob{bytes:ptr::null_mut(),length:0};
  *metadata=Blob{bytes:ptr::null_mut(),length:0};
 }
 match unsafe {attempt(error,cap,|| {
  let data=input(bytes,len,256*1024*1024)?;
  let json=str::from_utf8(input(profile,profile_len,1024*1024)?).map_err(|e|e.to_string())?;
  let handle=deserialize_render_handle(data,json)?;
  let (v,f,m)=render_outputs(&handle)?;
  Ok((v,f,m,handle))
 })} {
  Some((v,f,m,handle))=>{unsafe {*vertex_spirv=blob(v);*fragment_spirv=blob(f);*metadata=blob(m)};Box::into_raw(Box::new(handle))},
  None=>ptr::null_mut(),
 }
}
#[repr(C)]
pub struct Blob { pub bytes: *mut u8, pub length: usize }
fn blob(bytes: Vec<u8>) -> Blob {
    let mut data=bytes.into_boxed_slice();
    let result=Blob {bytes:data.as_mut_ptr(), length:data.len()};
    std::mem::forget(data); result
}
unsafe fn input<'a>(bytes:*const u8,len:usize,max:usize) -> Result<&'a [u8],String> {
    if bytes.is_null() || len==0 || len>max { return Err("invalid input span".into()); }
    Ok(unsafe { slice::from_raw_parts(bytes,len) })
}
unsafe fn error_copy(error:*mut c_char,capacity:usize,message:&str) {
    if !error.is_null() && capacity>0 {
        let n=message.len().min(capacity-1);
        unsafe { ptr::copy_nonoverlapping(message.as_ptr(),error.cast(),n); *error.add(n)=0; }
    }
}
unsafe fn attempt<T>(error:*mut c_char,cap:usize,f:impl FnOnce()->Result<T,String>) -> Option<T> {
    unsafe { error_copy(error,cap,""); }
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(value))=>Some(value),
        Ok(Err(e))=>{unsafe {error_copy(error,cap,&e)};None},
        Err(_)=>{unsafe {error_copy(error,cap,"translator panic contained at C ABI")};None}
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_library_open(bytes:*const u8,len:usize,error:*mut c_char,cap:usize)->*mut Library {
    unsafe { attempt(error,cap,|| {
        let data=input(bytes,len,64*1024*1024)?;
        Library::parse(data).map(Box::new).map(Box::into_raw).map_err(|e|e.to_string())
    }) }.unwrap_or(ptr::null_mut())
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_library_close(library:*mut Library) {
    if !library.is_null() { drop(unsafe {Box::from_raw(library)}); }
}
// lowering-air-opus: AIR-direct sources (tegu_mali_compile_air_pair_options).
#[repr(C)]
pub struct AirSourceConstant { pub name:*const u8, pub name_len:usize, pub index:u32, pub size:u32, pub bits:u64 }
/// 0 ok; 1 invalid arguments; 2 a value wider than 8 bytes (AIR-direct refuses it; caller keeps m2v);
/// 3 more constants than `cap`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_library_air_source(library:*const Library,name:*const u8,name_len:usize,
 bytes:*mut *const u8,length:*mut usize,constants:*mut AirSourceConstant,cap:usize,count:*mut usize)->i32 {
 if library.is_null()||bytes.is_null()||length.is_null()||count.is_null()||(cap>0&&constants.is_null()){return 1;}
 let Ok(name)=(unsafe{input(name,name_len,4096)}) else {return 1};
 let Ok(name)=str::from_utf8(name) else {return 1};
 let lib=unsafe{&*library};
 let raw=lib.raw.bytes();
 unsafe{*bytes=raw.as_ptr();*length=raw.len();*count=0;}
 let Some(values)=lib.constants.get(name) else {return 0};
 let mut n=0usize;
 let mut put=|nm:Option<&str>,index:u32,v:&FunctionConstantValue|->i32 {
  if v.bytes.len()>8{return 2;} if n>=cap{return 3;}
  let mut b=[0u8;8];b[..v.bytes.len()].copy_from_slice(&v.bytes);
  unsafe{*constants.add(n)=AirSourceConstant{name:nm.map_or(ptr::null(),|s|s.as_ptr()),name_len:nm.map_or(0,|s|s.len()),
   index,size:v.bytes.len() as u32,bits:u64::from_le_bytes(b)};}
  n+=1;0
 };
 for (k,v) in &values.by_name {let r=put(Some(k.as_str()),0,v);if r!=0{return r;}}
 for (i,v) in &values.by_index {let r=put(None,*i,v);if r!=0{return r;}}
 unsafe{*count=n;}0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_function_count(library:*const Library)->usize {
    if library.is_null() {0} else {unsafe {&*library}.functions().count()}
}
// Function types match public MTLFunctionType (unknown/exported=0).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_function_info(library:*const Library,index:usize,name:*mut c_char,capacity:usize,kind:*mut u32)->usize {
    if library.is_null() || kind.is_null() {return 0;}
    let Some(info)=(unsafe {&*library}).functions().nth(index) else {return 0;};
    let stage=match info.stage {FunctionStage::Vertex=>1,FunctionStage::Fragment=>2,
        FunctionStage::Kernel=>3,FunctionStage::Visible=>5,FunctionStage::Intersection=>6,
        FunctionStage::Mesh=>7,FunctionStage::Object=>8,_=>0};
    unsafe {*kind=stage;error_copy(name,capacity,info.name);}
    info.name.len()+1 // required bytes including NUL; short destination is truncated
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_create(library:*const Library,name:*const u8,name_len:usize,
 profile:*const u8,profile_len:usize,spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut Pipeline {
    unsafe {tegu_air_pipeline_create_ex(library,name,name_len,ptr::null(),profile,profile_len,spirv,metadata,error,cap)}
}
/// One `MTLLinkedFunctions` entry: a function of `library` (any parsed or specialized library
/// handle) by name, in list 0 `functions`, 1 `privateFunctions` or 2 `binaryFunctions`.
#[repr(C)]
pub struct LinkedFunctionInput {pub library:*const Library,pub name:*const u8,pub name_len:usize,
    pub link_name:*const u8,pub link_name_len:usize,pub list:u32,pub reserved:u32}
/// `MTLComputePipelineDescriptor` inputs beyond the kernel (version 1). A NULL options pointer
/// is exactly `tegu_air_pipeline_create`.
#[repr(C)]
pub struct ComputeCreateOptions {
    pub version:u32, pub max_call_stack_depth:u32,
    pub linked:*const LinkedFunctionInput, pub linked_count:usize,
    pub stage_attributes:*const VertexAttribute, pub stage_attribute_count:usize,
    pub stage_layouts:*const VertexLayout, pub stage_layout_count:usize,
    pub stage_index_type:u32, pub stage_index_buffer_index:u32,
    pub has_stage_input:u32, pub reserved:u32,
}
const _:()=assert!(size_of::<ComputeCreateOptions>()==72&&size_of::<LinkedFunctionInput>()==48);
unsafe fn compute_options(base:FunctionDescriptor,options:*const ComputeCreateOptions)->Result<(FunctionDescriptor,ComputeExtra),String> {
    use metal2vulkan::{metal::{AttributeFormat,IndexType,StepFunction},pipeline::{LinkedFunction,LinkedFunctions,
        StageInputDescriptor,StageInputAttributeDescriptor,StageInputLayoutDescriptor}};
    if options.is_null() {return Ok((base,ComputeExtra::default()));}
    let o=unsafe {&*options};
    if o.version!=1||o.reserved!=0 {return Err("unsupported compute create options version".into());}
    if o.linked_count>4096||o.stage_attribute_count>31||o.stage_layout_count>31||
       (o.linked_count>0&&o.linked.is_null())||(o.stage_attribute_count>0&&o.stage_attributes.is_null())||
       (o.stage_layout_count>0&&o.stage_layouts.is_null()) {return Err("invalid compute create options".into());}
    let mut linked=LinkedFunctions::new();
    for i in 0..o.linked_count {
        let l=unsafe {&*o.linked.add(i)};
        if l.library.is_null()||l.reserved!=0 {return Err("invalid linked function".into());}
        let name=str::from_utf8(unsafe {input(l.name,l.name_len,4096)}?).map_err(|e|e.to_string())?;
        let lib=unsafe {&*l.library};
        let f=lib.raw.function(name).map_err(|e|e.to_string())?;
        let mut entry=LinkedFunction::new(f).with_constants(lib.constants.get(name).cloned().unwrap_or_default());
        // MTLFunctionDescriptor.specializedName: the name visible-function references use.
        if l.link_name_len>0 {
            let alias=str::from_utf8(unsafe {input(l.link_name,l.link_name_len,4096)}?).map_err(|e|e.to_string())?;
            if alias!=name {entry=entry.with_link_name(alias);}
        }
        match l.list {
            0=>linked.functions.push(entry),
            1=>linked.private_functions.push(entry),
            2=>linked.binary_functions.push(entry.with_precompiled(true)),
            _=>return Err("linked function list must be 0, 1 or 2".into()),
        }
    }
    let function=if linked.is_empty() {base} else {base.with_linked(linked)};
    let mut extra=ComputeExtra{stage_input:None,max_call_stack_depth:o.max_call_stack_depth.max(1)};
    if o.has_stage_input!=0 {
        let mut d=StageInputDescriptor::new();
        for i in 0..o.stage_layout_count {
            let l=unsafe {&*o.stage_layouts.add(i)};
            if l.index>=31 {return Err("stage input layout index out of range".into());}
            let step=StepFunction::try_from(l.step_function).map_err(|e|e.to_string())?;
            let mut layout=StageInputLayoutDescriptor::new(Some(l.stride));layout.step_function=step;layout.step_rate=l.step_rate;
            if d.layouts.insert(l.index,layout).is_some()
                {return Err("duplicate stage input layout".into());}
        }
        for i in 0..o.stage_attribute_count {
            let a=unsafe {&*o.stage_attributes.add(i)};
            if a.index>=31||a.buffer_index>=31 {return Err("stage input attribute out of range".into());}
            let format=AttributeFormat::try_from(a.format).map_err(|e|e.to_string())?;
            if d.attributes.insert(a.index,StageInputAttributeDescriptor::new(format,a.offset,a.buffer_index)).is_some()
                {return Err("duplicate stage input attribute".into());}
        }
        d.index_type=IndexType::try_from(o.stage_index_type).map_err(|e|e.to_string())?;
        d.index_buffer_index=o.stage_index_buffer_index;
        extra.stage_input=Some(d);
    }
    Ok((function,extra))
}
/// Tile render pipeline (MTLTileRenderPipelineDescriptor; shader-coverage-opus): the tile
/// function compiled under its colour attachment formats (formats[8], Metal pixel formats, 0 =
/// unset) as a compute pipeline the runtime dispatches once per tile in place of
/// dispatchThreadsPerTile (translator `compile_tile_pipeline`). Same handle and outputs as
/// tegu_air_pipeline_create; metadata "tile_pipeline" is set. Dispatch-time compute variants of
/// the handle are not tile-aware yet: the runtime must use only the creation compile.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_tile_pipeline_create(library:*const Library,name:*const u8,name_len:usize,
 formats:*const u32,raster_sample_count:u32,threadgroup_size_matches_tile_size:u32,
 profile:*const u8,profile_len:usize,spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut Pipeline {
    use metal2vulkan::{metal::PixelFormat,pipeline::TileRenderPipelineDescriptor};
    if spirv.is_null()||metadata.is_null()||spirv==metadata||formats.is_null() {return ptr::null_mut();}
    unsafe {*spirv=Blob{bytes:ptr::null_mut(),length:0};*metadata=Blob{bytes:ptr::null_mut(),length:0};}
    let result=unsafe {attempt(error,cap,|| {
        if library.is_null() {return Err("null library".into());}
        let entry=str::from_utf8(input(name,name_len,4096)?).map_err(|e|e.to_string())?;
        let json=str::from_utf8(input(profile,profile_len,1024*1024)?).map_err(|e|e.to_string())?;
        let device=device_for_profile(json)?;
        let function=(&*library).descriptor(entry)?;
        let mut d=TileRenderPipelineDescriptor::new(StageSource::Function(function.clone()));
        for i in 0..8usize {
            let f=*formats.add(i);
            if f!=0 {d.color_attachment_formats[i]=Some(PixelFormat::try_from(f).map_err(|e|e.to_string())?);}
        }
        d.raster_sample_count=raster_sample_count.max(1);
        d.threadgroup_size_matches_tile_size=threadgroup_size_matches_tile_size!=0;
        let pipeline=device.compile_tile_pipeline(&d,&Budget::standard()).map_err(|e|e.to_string())?;
        let stage=&pipeline.stage;
        let meta=serde_json::json!({"schema":1,"profile":pipeline.profile,"entry_point":stage.entry_point,
            "specialization":stage.specialization,"layout":pipeline.layout,"interface":pipeline.interface,
            "metal_info":pipeline.metal_info,"tile_pipeline":{"formats":(0..8).map(|i|*formats.add(i)).collect::<Vec<_>>(),
            "raster_sample_count":d.raster_sample_count}});
        let encoded=serde_json::to_vec(&meta).map_err(|e|e.to_string())?;
        let bytes:Vec<u8>=stage.spirv.iter().flat_map(|w|w.to_le_bytes()).collect();
        Ok((bytes,encoded,Pipeline {device,pipeline,creation_only:false,function,
            variants:std::sync::Mutex::new(Vec::new()),extra:ComputeExtra::default()}))
    })};
    match result {Some((bytes,meta,pipeline))=>{unsafe {*spirv=blob(bytes);*metadata=blob(meta)};Box::into_raw(Box::new(pipeline))},None=>ptr::null_mut()}
}
/// MTLFunction.patchType (MTLPatchType: 0 none, 1 triangle, 2 quad) and patchControlPointCount
/// (-1 when the function states none) of a library function (shader-coverage-opus). 0 ok.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_function_patch(library:*const Library,name:*const u8,name_len:usize,
 patch_type:*mut u32,control_points:*mut i32)->i32 {
    if library.is_null()||patch_type.is_null()||control_points.is_null() {return -1;}
    let r=catch_unwind(AssertUnwindSafe(|| -> Result<(u32,i32),String> {
        let entry=str::from_utf8(unsafe {input(name,name_len,4096)}?).map_err(|e|e.to_string())?;
        let f=unsafe {&*library}.raw.function(entry).map_err(|e|e.to_string())?;
        let p=f.properties(&Budget::standard()).map_err(|e|e.to_string())?;
        Ok((p.patch_type().as_raw() as u32,p.patch_control_point_count().map_or(-1,|n|n as i32)))
    }));
    match r {Ok(Ok((t,n)))=>{unsafe {*patch_type=t;*control_points=n;}0},_=>-1}
}
/// MTLComputePipelineState functionHandleWithFunction: (shader-coverage-opus): the table word of a
/// function the pipeline links in `functions` or `binaryFunctions` (identified as when it was
/// linked: library handle, name, optional link_name = specializedName). 0 ok (*word set),
/// 1 the pipeline cannot place it in a table, -1 invalid input.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_function_handle(pipeline:*const Pipeline,library:*const Library,
 name:*const u8,name_len:usize,link_name:*const u8,link_name_len:usize,word:*mut u32)->i32 {
    use metal2vulkan::pipeline::LinkedFunction;
    if pipeline.is_null()||library.is_null()||word.is_null() {return -1;}
    let r=catch_unwind(AssertUnwindSafe(|| -> Result<Option<u32>,String> {
        let entry=str::from_utf8(unsafe {input(name,name_len,4096)}?).map_err(|e|e.to_string())?;
        let lib=unsafe {&*library};
        let mut f=LinkedFunction::new(lib.raw.function(entry).map_err(|e|e.to_string())?)
            .with_constants(lib.constants.get(entry).cloned().unwrap_or_default());
        if link_name_len>0 {
            let alias=str::from_utf8(unsafe {input(link_name,link_name_len,4096)}?).map_err(|e|e.to_string())?;
            if alias!=entry {f=f.with_link_name(alias);}
        }
        Ok(unsafe {&*pipeline}.function.linked.function_handle(&f).map(|h|h.word()))
    }));
    match r {Ok(Ok(Some(w)))=>{unsafe {*word=w;}0},Ok(Ok(None))=>1,_=>-1}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_create_ex(library:*const Library,name:*const u8,name_len:usize,
 options:*const ComputeCreateOptions,
 profile:*const u8,profile_len:usize,spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->*mut Pipeline {
    if spirv.is_null()||metadata.is_null()||spirv==metadata {return ptr::null_mut();}
    unsafe {*spirv=Blob{bytes:ptr::null_mut(),length:0};*metadata=Blob{bytes:ptr::null_mut(),length:0};}
    let result=unsafe {attempt(error,cap,|| {
        if library.is_null() {return Err("null library".into());}
        let entry=str::from_utf8(input(name,name_len,4096)?).map_err(|e|e.to_string())?;
        let json=str::from_utf8(input(profile,profile_len,1024*1024)?).map_err(|e|e.to_string())?;
        let device=device_for_profile(json)?;
        let (function,extra)=compute_options((&*library).descriptor(entry)?,options)?;
        let budget=Budget::standard();
        let ((_,pipeline),creation_key,creation_only)=compile_compute_creation_ex(&device,&function,&budget,&extra).map_err(|e|e.to_string())?;
        let stage=&pipeline.stage;
        let validation=device.profile().spirv_validation();
        let meta=serde_json::json!({"schema":1,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},"profile":pipeline.profile,"entry_point":stage.entry_point,
            "specialization":stage.specialization,"required_subgroup_size":stage.required_subgroup_size,
            "stage_flags":stage.stage_flags,"precision_notes":stage.precision_notes,
            "reconvergence":stage.reconvergence,"constant_blob":stage.constant_blob.as_deref(),
            "layout":pipeline.layout,"interface":pipeline.interface,"dependencies":pipeline.dependencies,
            "metal_info":pipeline.metal_info,"variant_bits":pipeline.variant_bits,
            "creation_compile":{"kind":if creation_only {"storage_texture_stand_in"} else {"ordinary"},
             "dispatchable":!creation_only,"variant":creation_key}});
        let encoded=serde_json::to_vec(&meta).map_err(|e|e.to_string())?;
        let bytes:Vec<u8>=stage.spirv.iter().flat_map(|w|w.to_le_bytes()).collect();
        Ok((bytes,encoded,Pipeline {device,pipeline,creation_only,function,
            variants:std::sync::Mutex::new(Vec::new()),extra}))
    })};
    match result {Some((bytes,meta,pipeline))=>{unsafe {*spirv=blob(bytes);*metadata=blob(meta)};Box::into_raw(Box::new(pipeline))},None=>ptr::null_mut()}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_limits(pipeline:*const Pipeline,limits:*mut u32)->i32 {
    if pipeline.is_null()||limits.is_null() {return -1;}
    let info=&unsafe {&*pipeline}.pipeline.metal_info;
    unsafe {*limits=info.max_total_threads_per_threadgroup;*limits.add(1)=info.thread_execution_width;*limits.add(2)=info.static_threadgroup_memory_length;}
    0
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_dispatchable(pipeline:*const Pipeline)->i32 {
    if pipeline.is_null() {-1} else if unsafe {&*pipeline}.creation_only {0} else {1}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_close(pipeline:*mut Pipeline) {
    if !pipeline.is_null() { drop(unsafe {Box::from_raw(pipeline)}); }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_compile_compute(library:*const Library,name:*const u8,name_len:usize,
 profile:*const u8,profile_len:usize,spirv:*mut Blob,metadata:*mut Blob,error:*mut c_char,cap:usize)->i32 {
    let pipeline=unsafe {tegu_air_pipeline_create(library,name,name_len,profile,profile_len,spirv,metadata,error,cap)};
    if pipeline.is_null() {-1} else {unsafe {tegu_air_pipeline_close(pipeline)};0}
}
#[repr(C)]
pub struct BufferBinding { pub index:u32, pub reserved:u32, pub length:u64, pub offset:u64 }
/// Metal's setThreadgroupMemoryLength table is sparse and has 31 argument
/// slots. A NULL pointer is the legacy no-dynamic-memory request. Values are
/// bytes and must satisfy Metal's 16-byte multiple and 32 KiB limit; they are
/// copied into the StageTable below so the translator's TgLength SpecKeys (and
/// therefore every region's WLS size) are specialized before planning.
unsafe fn apply_threadgroup_lengths(table:&mut StageTable,lengths:*const u32)->Result<(),String> {
    if lengths.is_null(){return Ok(());}
    for i in 0..31usize {
        let length=unsafe {*lengths.add(i)};
        if length==0 {continue;}
        if length%16!=0||length>32768 {
            return Err("threadgroup memory length must be a 16-byte multiple <= 32768".into());
        }
        table.threadgroup_lengths.insert(i as u32,length);
    }
    Ok(())
}
unsafe fn plan_dispatch(pipeline:*const Pipeline,buffers:*const BufferBinding,count:usize,
 lengths:*const u32,groups:*const u32,local:*const u32)->Result<(BindingPlan,DispatchCommands),String> {
    unsafe {
        if pipeline.is_null()||groups.is_null()||local.is_null()||count>31||(count>0&&buffers.is_null()) {
            return Err("invalid pipeline dispatch arguments".into());
        }
        if (*pipeline).creation_only {
            return Err("storage texture stand-in pipeline is creation-only; dispatch variant unavailable".into());
        }
        let mut table=StageTable::default();
        apply_threadgroup_lengths(&mut table,lengths)?;
        for i in 0..count {
            let b=&*buffers.add(i);
            if b.reserved!=0||b.index>=31||b.offset>=b.length||table.buffers.contains_key(&b.index) {
                return Err("invalid or duplicate Metal buffer binding".into());
            }
            table.buffers.insert(b.index,BoundBuffer::Buffer {
                length:b.length,offset:b.offset,gpu_address:0,device_address:None,attribute_stride:None
            });
        }
        let mut state=BindState::default();state.stages.insert(Stage::Kernel,table);
        let p=&*pipeline;
        let plan=p.pipeline.plan(&p.device,&state.shape()).map_err(|e|e.to_string())?;
        if !plan.variant.is_empty() {return Err("dispatch requires a different compiled pipeline variant".into());}
        let dispatch=Dispatch::Threadgroups {
            groups:[*groups,*groups.add(1),*groups.add(2)],
            threads_per_group:[*local,*local.add(1),*local.add(2)]
        };
        let commands=plan.instantiate_dispatch(&state,&dispatch).map_err(|e|e.to_string())?;
        Ok((plan,commands))
    }
}
// Return the complete translator plan, including specializations and auxiliary
// resources. Consumers must honor it; descriptor indices are NOT Metal slots.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_plan(pipeline:*const Pipeline,
 buffers:*const BufferBinding,count:usize,groups:*const u32,local:*const u32,
 output:*mut Blob,error:*mut c_char,cap:usize)->i32 {
    if output.is_null() {return -1;}
    unsafe {*output=Blob{bytes:ptr::null_mut(),length:0};}
    let result=unsafe {attempt(error,cap,|| {
        let (plan,commands)=plan_dispatch(pipeline,buffers,count,ptr::null(),groups,local)?;
        serde_json::to_vec(&serde_json::json!({"schema":1,"plan":plan,"commands":commands})).map_err(|e|e.to_string())
    })};
    match result {Some(bytes)=>{unsafe {*output=blob(bytes)};0},None=>-1}
}
#[repr(C)]
#[derive(Clone,Copy,Default)]
pub struct NativeBinding { pub index:u32,pub reserved:u32,pub offset:u64,pub length:u64 }
#[repr(C)]
#[derive(Clone,Copy,Default)]
pub struct NativeConstant { pub id:u32,pub size:u32,pub data:[u8;8] }
#[repr(C)]
pub struct NativeDispatch {
 pub version:u32,pub bindings_mask:u32,pub groups:[u32;3],pub constant_count:u32,
 pub bindings:[NativeBinding;8],pub push:[u8;128],pub constants:[NativeConstant;64]
}
fn native_dispatch(plan:&BindingPlan,commands:&DispatchCommands)->Result<NativeDispatch,String> {
    if commands.regions.len()!=1 || !commands.descriptor_bytes.is_empty() ||
       !commands.push_holes.is_empty() || commands.root_table.is_some() || commands.indirect_clamp.is_some() ||
       !commands.uses.is_empty() || !plan.vertex_buffers.is_empty() || plan.graphics.is_some() {
        return Err("native dispatch requires unsupported auxiliary resources or regions".into());
    }
    let region=&commands.regions[0];
    if region.indirect.is_some()||region.required_subgroup_size.is_some()||region.stage_flags!=vk::PipelineShaderStageCreateFlags::empty() {
        return Err("native dispatch does not implement indirect/subgroup requirements".into());
    }
    let mut out:NativeDispatch=unsafe {std::mem::zeroed()};
    out.version=1;out.groups=region.group_count;
    for descriptor in &plan.descriptors {
        if descriptor.set!=0||descriptor.binding>=8||descriptor.array_element!=0||
           !matches!(descriptor.ty,vk::DescriptorType::STORAGE_BUFFER|vk::DescriptorType::UNIFORM_BUFFER) {
            return Err("native descriptor layout exceeds buffer ABI".into());
        }
        let WriteSource::Buffer{slot}=descriptor.source else {return Err("native descriptor requires a non-buffer resource".into());};
        if slot.stage!=Stage::Kernel||slot.class!=SlotClass::Buffer||slot.index>=31 {
            return Err("invalid native buffer slot".into());
        }
        let bit=1u32<<descriptor.binding;
        if out.bindings_mask&bit!=0 {return Err("duplicate native descriptor".into());}
        let range=commands.descriptor_ranges.iter().find(|r|r.set==0&&r.binding==descriptor.binding)
            .ok_or("missing native descriptor range")?;
        if range.range.range==0||range.range.offset%4!=0||range.range.range>4096-(range.range.offset%4096) {
            return Err("native buffer view exceeds a GPU page".into());
        }
        out.bindings_mask|=bit;
        out.bindings[descriptor.binding as usize]=NativeBinding{index:slot.index,reserved:0,offset:range.range.offset,length:range.range.range};
    }
    if commands.descriptor_ranges.len()!=plan.descriptors.len() {return Err("unhandled descriptor range".into());}
    // The kernel flushes and retains every mapped buffer, synchronously fences
    // each dispatch, and flushes GPU writes back. That covers these buffer uses.
    for usage in &plan.uses {
        let UseTarget::Slot(slot)=usage.resource else {return Err("native dispatch has an unbound resource use".into());};
        if slot.stage!=Stage::Kernel||slot.class!=SlotClass::Buffer||
           !matches!(usage.class,AccessClass::Storage|AccessClass::Uniform)||
           !out.bindings.iter().enumerate().any(|(i,b)|out.bindings_mask&(1<<i)!=0&&b.index==slot.index) {
            return Err("native resource use is not covered by mapped buffers".into());
        }
    }
    let mut written=[false;128];
    for push in &region.push_constants {
        let start=push.offset as usize;
        let end=start.checked_add(push.bytes.len()).ok_or("push range overflow")?;
        if push.stages!=vk::ShaderStageFlags::COMPUTE||end>128 {return Err("native push range exceeds ABI".into());}
        for (i,&byte) in push.bytes.iter().enumerate() {
            let at=start+i;
            if written[at]&&out.push[at]!=byte {return Err("conflicting push writes".into());}
            out.push[at]=byte;written[at]=true;
        }
    }
    if region.specialization.entries.len()>64 {return Err("too many native specialization constants".into());}
    for (i,entry) in region.specialization.entries.iter().enumerate() {
        let n=entry.size as usize;
        let start=entry.offset as usize;
        let end=start.checked_add(n).ok_or("specialization overflow")?;
        if !matches!(n,1|2|4|8)||end>region.specialization.data.len()||out.constants[..i].iter().any(|s|s.id==entry.constant_id) {
            return Err("invalid native specialization".into());
        }
        out.constants[i].id=entry.constant_id;out.constants[i].size=n as u32;
        out.constants[i].data[..n].copy_from_slice(&region.specialization.data[start..end]);
    }
    out.constant_count=region.specialization.entries.len() as u32;
    Ok(out)
}
unsafe fn native_dispatch_impl(pipeline:*const Pipeline,buffers:*const BufferBinding,count:usize,
 lengths:*const u32,groups:*const u32,local:*const u32,output:*mut NativeDispatch,
 error:*mut c_char,cap:usize)->i32 {
    if output.is_null() {return -1;}
    unsafe {*output=std::mem::zeroed();}
    let result=unsafe {attempt(error,cap,|| {
        let (plan,commands)=plan_dispatch(pipeline,buffers,count,lengths,groups,local)?;
        native_dispatch(&plan,&commands)
    })};
    match result {Some(value)=>{unsafe {*output=value};0},None=>-1}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_native_dispatch(pipeline:*const Pipeline,
 buffers:*const BufferBinding,count:usize,groups:*const u32,local:*const u32,
 output:*mut NativeDispatch,error:*mut c_char,cap:usize)->i32 {
    unsafe {native_dispatch_impl(pipeline,buffers,count,ptr::null(),groups,local,output,error,cap)}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_native_dispatch_tg(pipeline:*const Pipeline,
 buffers:*const BufferBinding,count:usize,lengths:*const u32,groups:*const u32,local:*const u32,
 output:*mut NativeDispatch,error:*mut c_char,cap:usize)->i32 {
    unsafe {native_dispatch_impl(pipeline,buffers,count,lengths,groups,local,output,error,cap)}
}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct ComputeResource {
    pub version:u32,pub kind:u32,pub index:u32,pub access:u32,
    pub format:u32,pub texture_type:u32,pub width:u32,pub height:u32,
    pub depth:u32,pub array_length:u32,pub mipmap_levels:u32,pub sample_count:u32,
    pub usage:u32,pub row_stride:u32,pub storage_mode:u32,pub swizzle_word:u32,
    pub slice_stride:u32,pub layer_stride:u32,pub array_layers:u32,pub reserved:u32,
    pub min_filter:u32,pub mag_filter:u32,pub mip_filter:u32,
    pub address_s:u32,pub address_t:u32,pub address_r:u32,pub border_color:u32,
    pub normalized_coordinates:u32,pub lod_average:u32,pub max_anisotropy:u32,
    pub compare_function:u32,pub reduction_mode:u32,
    pub lod_min_clamp:f32,pub lod_max_clamp:f32,
    pub length:u64,pub offset:u64,pub resource_id:u64,
    pub swizzle_valid:u32,pub reserved2:u32,
    pub level_offset:[u64;TEGU_AIR_COMPUTE_MAX_LEVELS],
    pub level_row_stride:[u32;TEGU_AIR_COMPUTE_MAX_LEVELS],
    pub level_slice_stride:[u32;TEGU_AIR_COMPUTE_MAX_LEVELS],
    pub level_layer_stride:[u32;TEGU_AIR_COMPUTE_MAX_LEVELS],
}
#[repr(C)]
#[derive(Clone,Copy,Default)]
pub struct ComputeDescriptor {
    pub binding:u32,pub kind:u32,pub metal_index:u32,pub flags:u32,
    pub sampler_index:u32,pub sampler_flags:u32,pub view_format:u32,pub view_level:u32,
    pub view_layer:u32,pub reserved:u32,pub view_format_vk:u32,pub reserved2:u32,
    pub range_offset:u64,pub range_length:u64,
}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct ComputeRegion {
    pub groups:[u32;3],pub local:[u32;3],pub flags:u32,pub constant_count:u32,
    pub constants:[NativeConstant;64],pub push:[u8;128],
}
impl Default for ComputeRegion {fn default()->Self{Self{groups:[0;3],local:[0;3],flags:0,constant_count:0,
    constants:[NativeConstant::default();64],push:[0;128]}}}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct NativeDispatchV2 {
    pub version:u32,pub region_count:u32,pub descriptor_count:u32,pub local:[u32;3],
    pub flags:u32,pub reserved:[u32;2],pub variant_hash:[u8;32],
    pub descriptors:[ComputeDescriptor;TEGU_AIR_COMPUTE_MAX_DESCRIPTORS],
    pub regions:[ComputeRegion;TEGU_AIR_COMPUTE_MAX_REGIONS],
}
impl Default for NativeDispatchV2 {fn default()->Self{Self{version:0,region_count:0,descriptor_count:0,
    local:[0;3],flags:0,reserved:[0;2],variant_hash:[0;32],
    descriptors:[ComputeDescriptor::default();TEGU_AIR_COMPUTE_MAX_DESCRIPTORS],
    regions:[ComputeRegion::default();TEGU_AIR_COMPUTE_MAX_REGIONS]}}}
pub const TEGU_AIR_COMPUTE_MAX_RESOURCES:usize=32;
pub const TEGU_AIR_COMPUTE_MAX_LEVELS:usize=16;
pub const TEGU_AIR_COMPUTE_MAX_DESCRIPTORS:usize=32;
pub const TEGU_AIR_COMPUTE_MAX_REGIONS:usize=8;
pub const TEGU_AIR_COMPUTE_ACCESS_READ:u32=0x100;
pub const TEGU_AIR_COMPUTE_ACCESS_WRITE:u32=0x200;
pub const TEGU_AIR_COMPUTE_ACCESS_SAMPLE:u32=0x400;
pub const TEGU_AIR_COMPUTE_ACCESS_STORAGE:u32=0x800;
const _:()=assert!(size_of::<ComputeResource>()==488);
const _:()=assert!(size_of::<ComputeDescriptor>()==64);
const _:()=assert!(size_of::<ComputeRegion>()==1184);
pub const TEGU_AIR_COMPUTE_MAX_BUFFERS:usize=31;
const _:()=assert!(TEGU_AIR_COMPUTE_MAX_BUFFERS<=TEGU_AIR_COMPUTE_MAX_DESCRIPTORS);
const TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS:u32=0;
const TEGU_AIR_COMPUTE_DISPATCH_THREADS:u32=1;
const _:()=assert!(size_of::<NativeDispatchV2>()==11592);

fn metal_view_format(format:vk::Format)->Result<u32,String> {
    use metal2vulkan::metal::{PixelFormat,format::direct_format};
    PixelFormat::ALL.iter().find(|pf|direct_format(**pf)==Some(format))
        .map(|pf|pf.as_raw() as u32)
        .ok_or_else(||format!("compute view format {format:?} has no Metal pixel-format alias"))
}
fn compute_texture_facts(r:&ComputeResource)->Result<metal2vulkan::bind::TextureFacts,String> {
    use metal2vulkan::{bind::TextureFacts,metal::{PixelFormat,TextureType,TextureUsage}};
    if r.index>=31{return Err("compute texture index exceeds 31".into());}
    let ty=TextureType::try_from(r.texture_type).map_err(|e|e.to_string())?;
    let format=PixelFormat::try_from(r.format).map_err(|e|e.to_string())?;
    let usage=TextureUsage::try_from(r.usage).map_err(|e|e.to_string())?;
    if r.width==0||r.height==0||r.mipmap_levels==0||r.mipmap_levels as usize>TEGU_AIR_COMPUTE_MAX_LEVELS||r.sample_count==0||r.array_length==0||r.array_layers==0{
        return Err("compute texture dimensions/levels are invalid".into());}
    if r.texture_type==5||r.texture_type==6 {if r.array_layers!=6u32*r.array_length{return Err("cube texture face count is invalid".into());}}
    else if r.texture_type==1||r.texture_type==3||r.texture_type==8 {if r.array_layers!=r.array_length{return Err("array texture layer count is invalid".into());}}
    else if r.array_layers!=1{return Err("non-array texture has multiple layers".into());}
    if ty!=TextureType::Type3D&&r.depth!=1{return Err("only 3D textures may have depth != 1".into());}
    if ty==TextureType::Type3D&&r.depth==0{return Err("3D texture depth is zero".into());}
    if r.row_stride==0||r.length==0||r.offset>=r.length{return Err("compute texture backing span is invalid".into());}
    let Some(span)=compute_texture_span(r) else {return Err("compute texture backing span overflows".into());};
    if span>r.length-r.offset{return Err("compute texture exceeds its retained allocation".into());}
    if (r.access&(TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE))!=0&&
       !usage.contains(TextureUsage::SHADER_READ){return Err("sampled compute texture lacks shader-read usage".into());}
    if (r.access&TEGU_AIR_COMPUTE_ACCESS_STORAGE)!=0&&!usage.contains(TextureUsage::SHADER_WRITE){
        return Err("storage compute image lacks shader-write usage".into());}
    if r.swizzle_valid>1{return Err("compute texture swizzle-valid bit is invalid".into());}
    if r.swizzle_word!=0&&r.swizzle_word!=0x688{return Err("compute texture swizzle is not expressible yet".into());}
    if !compute_level_facts_ok(r){return Err("compute mip chain is not contiguous with the TDG2 level-1 stride convention".into());}
    let swizzle=TextureSwizzleChannels::DEFAULT;
    let mut facts=TextureFacts::new(ty,format);
    facts.sample_count=r.sample_count;facts.usage=usage;facts.array_length=r.array_length;
    facts.mipmap_level_count=r.mipmap_levels;facts.swizzle=swizzle;
    facts.sparse=false;facts.depth_twin=false;facts.parent_relative_level=0;
    facts.parent_base_size=[0;3];facts.own_format_views=false;
    Ok(facts)
}
fn compute_texture_span(r:&ComputeResource)->Option<u64> {
    let levels=r.mipmap_levels;
    let layers=u64::from(r.array_layers.max(1));
    if levels<=1 {
        let depth=u64::from(r.depth.max(1));
        let rows=u64::from(r.height).checked_mul(depth)?;
        let plane=u64::from(r.row_stride).checked_mul(rows)?;
        return plane.checked_mul(layers);
    }
    if levels as usize>TEGU_AIR_COMPUTE_MAX_LEVELS{return None;}
    let mut offset=0u64;
    for level in 0..levels {
        // The TDG2 v2 wire carries one contiguous linear chain: every level
        // starts after the previous level's span and keeps the level-0
        // row/slice/layer strides. Exact per-level facts live in the producer
        // ABI; a non-conforming chain is refused, never truncated to L0.
        if r.level_offset[level as usize]!=offset ||
           r.level_row_stride[level as usize]!=r.row_stride ||
           r.level_slice_stride[level as usize]!=r.slice_stride ||
           r.level_layer_stride[level as usize]!=r.layer_stride {return None;}
        let width=minify_texel(r.width,level);
        let height=minify_texel(r.height,level);
        let depth=if r.texture_type==7{minify_texel(r.depth,level)}else{1};
        if width==0||height==0||depth==0{return None;}
        let mut size=u64::from(r.row_stride).checked_mul(u64::from(height))?;
        if r.texture_type==7 {
            size=size.checked_add(u64::from(depth.saturating_sub(1)).checked_mul(u64::from(r.slice_stride))?)?;
        } else if r.sample_count>1 {
            size=size.checked_add(u64::from(r.sample_count-1).checked_mul(u64::from(r.slice_stride))?)?;
        }
        offset=offset.checked_add(size)?;
    }
    if layers>1 {
        let stride=u64::from(r.layer_stride);
        if stride==0{return None;}
        offset=offset.checked_add((layers-1).checked_mul(stride)?)?;
    }
    Some(offset)
}
fn minify_texel(value:u32,level:u32)->u32 {
    let mut v=value;
    for _ in 0..level {v=if v>1{v/2}else{1};}
    v
}
fn compute_level_facts_ok(r:&ComputeResource)->bool {
    if r.mipmap_levels<=1{return true;}
    if r.mipmap_levels as usize>TEGU_AIR_COMPUTE_MAX_LEVELS{return false;}
    for level in 0..r.mipmap_levels as usize {
        if r.level_row_stride[level]==0||r.level_row_stride[level]!=r.row_stride||
           r.level_slice_stride[level]!=r.slice_stride||r.level_layer_stride[level]!=r.layer_stride{
            return false;
        }
    }
    true
}
fn compute_sampler_desc(r:&ComputeResource)->Result<metal2vulkan::bind::SamplerDescriptor,String> {
    use metal2vulkan::{bind::SamplerDescriptor,metal::{CompareFunction,SamplerAddressMode,SamplerBorderColor,SamplerMinMagFilter,SamplerMipFilter,SamplerReductionMode}};
    if r.index>=16{return Err("compute sampler index exceeds 15".into());}
    if r.format!=0||r.texture_type!=0||r.width!=0||r.height!=0||r.depth!=0||r.array_length!=0||
       r.mipmap_levels!=0||r.sample_count!=0||r.usage!=0||r.row_stride!=0||r.length!=0||
       r.offset!=0||r.access!=0||r.resource_id!=0||r.storage_mode!=0||r.swizzle_word!=0||
       r.slice_stride!=0||r.layer_stride!=0{
        return Err("compute sampler carries irrelevant resource facts".into());}
    if r.normalized_coordinates>1||r.lod_average>1{return Err("compute sampler boolean field is invalid".into());}
    if !r.lod_min_clamp.is_finite()||!r.lod_max_clamp.is_finite()||r.lod_min_clamp>r.lod_max_clamp{
        return Err("compute sampler LOD clamps are not finite/ordered".into());}
    let mut d=SamplerDescriptor::new();
    d.min_filter=SamplerMinMagFilter::try_from(r.min_filter).map_err(|e|e.to_string())?;
    d.mag_filter=SamplerMinMagFilter::try_from(r.mag_filter).map_err(|e|e.to_string())?;
    d.mip_filter=SamplerMipFilter::try_from(r.mip_filter).map_err(|e|e.to_string())?;
    d.s_address_mode=SamplerAddressMode::try_from(r.address_s).map_err(|e|e.to_string())?;
    d.t_address_mode=SamplerAddressMode::try_from(r.address_t).map_err(|e|e.to_string())?;
    d.r_address_mode=SamplerAddressMode::try_from(r.address_r).map_err(|e|e.to_string())?;
    d.border_color=SamplerBorderColor::try_from(r.border_color).map_err(|e|e.to_string())?;
    d.normalized_coordinates=r.normalized_coordinates==1;
    d.lod_min_clamp=r.lod_min_clamp;d.lod_max_clamp=r.lod_max_clamp;
    d.lod_average=r.lod_average==1;
    if r.max_anisotropy==0{return Err("compute sampler max anisotropy is zero".into());}
    d.max_anisotropy=r.max_anisotropy;
    d.compare_function=CompareFunction::try_from(r.compare_function).map_err(|e|e.to_string())?;
    d.reduction_mode=SamplerReductionMode::try_from(r.reduction_mode).map_err(|e|e.to_string())?;
    Ok(d)
}
fn compute_state(resources:&[ComputeResource],lengths:*const u32)->Result<metal2vulkan::bind::BindState,String> {
    use std::collections::BTreeMap;
    use metal2vulkan::{bind::{BindState,BoundBuffer,BoundSampler,BoundTexture,StageTable},pipeline::Stage};
    if resources.len()>TEGU_AIR_COMPUTE_MAX_RESOURCES{return Err("compute resource count exceeds 32".into());}
    let mut table=StageTable::default();
    unsafe {apply_threadgroup_lengths(&mut table,lengths)?;}
    for r in resources {
        if r.version!=1{return Err("compute resource version mismatch".into());}
        if r.reserved!=0{return Err("compute resource reserved field is nonzero".into());}
        match r.kind {
            1=>{
                if r.index>=31{return Err("compute buffer index exceeds 31".into());}
                if table.buffers.contains_key(&r.index){return Err("duplicate compute buffer slot".into());}
                if r.length==0||r.offset>=r.length||(r.access&(TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_WRITE))==0||
                   (r.access&(TEGU_AIR_COMPUTE_ACCESS_SAMPLE|TEGU_AIR_COMPUTE_ACCESS_STORAGE))!=0{
                    return Err("compute buffer has an invalid access/range".into());}
                if r.format!=0||r.texture_type!=0||r.width!=0||r.height!=0||r.depth!=0||r.array_length!=0||
                   r.mipmap_levels!=0||r.sample_count!=0||r.usage!=0||r.row_stride!=0||r.storage_mode!=0||r.resource_id!=0||
                   r.min_filter!=0||r.mag_filter!=0||r.mip_filter!=0||r.address_s!=0||r.address_t!=0||
                   r.address_r!=0||r.border_color!=0||r.normalized_coordinates!=0||r.lod_average!=0||
                   r.max_anisotropy!=0||r.compare_function!=0||r.reduction_mode!=0||r.swizzle_word!=0||
                   r.slice_stride!=0||r.layer_stride!=0||
                   r.lod_min_clamp!=0.0||r.lod_max_clamp!=0.0{
                    return Err("compute buffer carries irrelevant texture/sampler facts".into());}
                table.buffers.insert(r.index,BoundBuffer::Buffer{length:r.length,offset:r.offset,
                    gpu_address:0,device_address:None,attribute_stride:None});
            },
            2=>{
                if table.textures.contains_key(&r.index){return Err("duplicate compute texture slot".into());}
                if r.access==0||(r.access&!(TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_WRITE|
                    TEGU_AIR_COMPUTE_ACCESS_SAMPLE|TEGU_AIR_COMPUTE_ACCESS_STORAGE))!=0{
                    return Err("compute texture access class is invalid".into());}
                if r.resource_id==0{return Err("compute texture resource id is zero".into());}
                if r.storage_mode>2{return Err("compute texture storage mode is unsupported".into());}
                if r.min_filter!=0||r.mag_filter!=0||r.mip_filter!=0||r.address_s!=0||r.address_t!=0||
                   r.address_r!=0||r.border_color!=0||r.normalized_coordinates!=0||r.lod_average!=0||
                   r.max_anisotropy!=0||r.compare_function!=0||r.reduction_mode!=0||
                   r.lod_min_clamp!=0.0||r.lod_max_clamp!=0.0{
                    return Err("compute texture carries irrelevant sampler facts".into());}
                let facts=compute_texture_facts(r)?;
                table.textures.insert(r.index,BoundTexture::Texture{facts,resource_id:Some(r.resource_id)});
            },
            3=>{
                if table.samplers.contains_key(&r.index){return Err("duplicate compute sampler slot".into());}
                let d=compute_sampler_desc(r)?;
                table.samplers.insert(r.index,BoundSampler::new(d));
            },
            _=>return Err("unknown compute resource kind".into()),
        }
    }
    let mut state=BindState::default();
    state.stages=BTreeMap::from([(Stage::Kernel,table)]);
    Ok(state)
}
fn compute_variant_plan(p:&Pipeline,state:&metal2vulkan::bind::BindState,base:BindingPlan)
 ->Result<(BindingPlan,Option<Vec<u8>>,Option<[u8;32]>),String> {
    use metal2vulkan::pipeline::Stage;
    if base.variant.is_empty(){return Ok((base,None,None));}
    let key=base.variant.clone();
    let mut cache=p.variants.lock().map_err(|_|"compute variant cache poisoned".to_string())?;
    if !cache.iter().any(|(k,_)|k==&key) {
        let compiled=p.device.compile_function(&p.function,&p.extra.inputs(&key),&Budget::standard())
            .map_err(|e|e.to_string())?;
        let pipeline=p.device.compile_compute_pipeline(&p.extra.descriptor(compiled),&Budget::standard())
            .map_err(|e|e.to_string())?;
        if cache.len()>=COMPUTE_VARIANTS{cache.remove(0);}
        cache.push((key.clone(),pipeline));
    }
    let index=cache.iter().position(|(k,_)|k==&key).ok_or("compute variant disappeared")?;
    let variant=&cache[index].1;
    let plan=variant.plan(&p.device,&state.shape()).map_err(|e|e.to_string())?;
    if !plan.variant.is_empty()&&plan.variant!=key {
        return Err("bound-format variant did not converge to the requested key".into());
    }
    let hash=plan.content_digest().map_err(|e|e.to_string())?;
    let bytes:Vec<u8>=variant.stage.spirv.iter().flat_map(|w|w.to_le_bytes()).collect();
    Ok((plan,Some(bytes),Some(hash)))
}
fn slot_use_flags(plan:&BindingPlan,slot:metal2vulkan::pipeline::SlotRef)->Result<u32,String> {
    let mut flags=0;
    for u in &plan.uses {
        let UseTarget::Slot(s)=u.resource else {return Err("compute plan has a non-slot resource use".into());};
        if s!=slot{continue;}
        match slot.class {
            SlotClass::Buffer=>{
                if u.access.read{flags|=TEGU_AIR_COMPUTE_ACCESS_READ;}
                if u.access.write{flags|=TEGU_AIR_COMPUTE_ACCESS_WRITE;}
            },
            SlotClass::Texture=>match u.class {
                AccessClass::Sampled=>flags|=TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,
                AccessClass::StorageImage=>{
                    flags|=TEGU_AIR_COMPUTE_ACCESS_STORAGE;
                    if u.access.read{flags|=TEGU_AIR_COMPUTE_ACCESS_READ;}
                    if u.access.write{flags|=TEGU_AIR_COMPUTE_ACCESS_WRITE;}
                },
                _=>return Err("compute texture slot has an unsupported access class".into()),
            },
            SlotClass::Sampler=>{},
        }
    }
    if slot.class==SlotClass::Buffer&&flags==0{flags=TEGU_AIR_COMPUTE_ACCESS_READ;}
    if slot.class==SlotClass::Texture&&flags==0{return Err("compute texture slot has no proven access".into());}
    Ok(flags)
}
const KIND_BUFFER_UNIFORM:u32=1;
const KIND_BUFFER_STORAGE:u32=2;
const KIND_TEXTURE:u32=3;
const KIND_IMAGE:u32=4;
const KIND_SAMPLER:u32=5;
const KIND_COMBINED:u32=6;
fn compute_native_dispatch(p:&Pipeline,resources:&[ComputeResource],lengths:*const u32,mode:u32,
 extents:[u32;3],local:[u32;3])->Result<(NativeDispatchV2,Option<Vec<u8>>,Option<[u8;32]>),String> {
    use metal2vulkan::{bind::{Dispatch,ImageSource,SamplerSource,UseTarget,WriteSource},pipeline::{SlotClass,SlotRef,Stage}};
    if mode>TEGU_AIR_COMPUTE_DISPATCH_THREADS{return Err("compute dispatch mode is invalid".into());}
    for i in 0..3 {
        if local[i]==0||local[i]>1024{return Err("compute threadgroup size is invalid".into());}
        if mode==TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS&&(extents[i]==0||extents[i]>65536){
            return Err("compute threadgroup count is invalid".into());}
    }
    let product=local.iter().try_fold(1u32,|a,&b|a.checked_mul(b)).ok_or("threadgroup size overflows")?;
    if product>256{return Err("compute threadgroup exceeds the 256-thread local-size contract".into());}
    let state=compute_state(resources,lengths)?;
    let dispatch=match mode {
        TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS=>Dispatch::Threadgroups{groups:extents,threads_per_group:local},
        _=>Dispatch::Threads{threads:extents,threads_per_group:local},
    };
    let base=p.pipeline.plan(&p.device,&state.shape()).map_err(|e|e.to_string())?;
    if p.creation_only&&base.variant.is_empty(){
        return Err("creation-only pipeline has no variant for the bound storage formats".into());
    }
    let (plan,variant_spirv,variant_hash)=compute_variant_plan(p,&state,base)?;
    let commands=plan.instantiate_dispatch(&state,&dispatch).map_err(|e|e.to_string())?;
    if !commands.descriptor_bytes.is_empty()||!commands.push_holes.is_empty()||commands.root_table.is_some()||
       commands.indirect_clamp.is_some()||!commands.uses.is_empty()||!plan.vertex_buffers.is_empty()||plan.graphics.is_some(){
        return Err("compute dispatch requires unsupported auxiliary resources".into());
    }
    if commands.regions.is_empty()||commands.regions.len()>TEGU_AIR_COMPUTE_MAX_REGIONS{
        return Err("compute dispatch exceeds the 8-region contract".into());
    }
    let mut out=NativeDispatchV2::default();out.version=2;out.local=local;
    let local_specs:Vec<(u32,usize)>=p.pipeline.stage.specialization.constants.iter()
        .filter_map(|c|match c.key {metal2vulkan::pipeline::SpecKey::LocalSize(axis)=>
            Some((c.id,axis as usize)),_=>None}).collect();
    out.region_count=commands.regions.len() as u32;
    let mut writes:Vec<&metal2vulkan::bind::DescriptorWrite>=plan.descriptors.iter().collect();
    writes.sort_by_key(|w|(w.set,w.binding));
    let find_resource=|kind:u32,index:u32|resources.iter().find(|r|r.kind==kind&&r.index==index);
    for (i,w) in writes.iter().enumerate() {
        if i>=TEGU_AIR_COMPUTE_MAX_DESCRIPTORS{return Err("compute plan has too many descriptors".into());}
        if w.set!=0||w.array_element!=0{return Err("compute descriptor outside set0 element0".into());}
        if w.binding>255{return Err("compute descriptor native binding exceeds 255".into());}
        let mut d=ComputeDescriptor{binding:w.binding,..Default::default()};
        let slot_flags=|slot:SlotRef|slot_use_flags(&plan,slot);
        match &w.source {
            WriteSource::Buffer{slot} if slot.class==SlotClass::Buffer=>{
                let kind=match w.ty {
                    vk::DescriptorType::UNIFORM_BUFFER=>KIND_BUFFER_UNIFORM,
                    vk::DescriptorType::STORAGE_BUFFER=>KIND_BUFFER_STORAGE,
                    _=>return Err("compute buffer descriptor has an unsupported type".into()),
                };
                if slot.stage!=Stage::Kernel||slot.index>=31{return Err("compute buffer Metal slot is invalid".into());}
                if w.binding>=TEGU_AIR_COMPUTE_MAX_BUFFERS as u32{
                    return Err("compute buffer native binding exceeds the extended table 0..30".into());
                }
                let r=find_resource(1,slot.index).ok_or("compute buffer is not bound")?;
                let range=commands.descriptor_ranges.iter().find(|r|r.set==0&&r.binding==w.binding)
                    .ok_or("compute buffer descriptor range is missing")?;
                if range.range.range==0||range.range.offset%4!=0||
                   range.range.offset>r.length||range.range.range>r.length-range.range.offset{
                    return Err("compute buffer descriptor exceeds its retained allocation".into());}
                let flags=slot_flags(*slot)?;
                if kind==KIND_BUFFER_UNIFORM&&(flags&TEGU_AIR_COMPUTE_ACCESS_READ)==0{return Err("compute uniform buffer has no read use".into());}
                d.kind=kind;d.metal_index=slot.index;d.flags=flags;
                d.range_offset=range.range.offset;d.range_length=range.range.range;
            },
            WriteSource::InlineBytes{..}=>return Err("compute inline-byte descriptors are not supported".into()),
            WriteSource::Texture{slot,view} if slot.class==SlotClass::Texture=>{
                let (kind,access)=match w.ty {
                    vk::DescriptorType::SAMPLED_IMAGE=>(KIND_TEXTURE,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE),
                    vk::DescriptorType::STORAGE_IMAGE=>(KIND_IMAGE,TEGU_AIR_COMPUTE_ACCESS_STORAGE),
                    _=>return Err("compute texture descriptor has an unsupported type".into()),
                };
                if view.twin||view.level.unwrap_or(0)!=0{return Err("compute texture view beyond level0/twin is not expressible".into());}
                if !view.shader_swizzle&&view.components!=vk::ComponentMapping::IDENTITY{return Err("compute texture view component mapping is not expressible".into());}
                if slot.stage!=Stage::Kernel{return Err("compute texture slot stage is invalid".into());}
                let r=find_resource(2,slot.index).ok_or("compute texture is not bound")?;
                if r.access&access!=access{return Err("compute texture access class does not match its bound facts".into());}
                let span=compute_texture_span(r).ok_or("compute texture span overflows")?;
                if r.offset>r.length||span>r.length-r.offset{
                    return Err("compute texture exceeds its retained allocation".into());}
                let mut use_flags=slot_flags(*slot)?;
                if kind==KIND_IMAGE&&(use_flags&(TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_WRITE))==0{use_flags|=TEGU_AIR_COMPUTE_ACCESS_READ;}
                d.kind=kind;d.metal_index=slot.index;d.flags=use_flags|access;
                d.view_format=metal_view_format(view.format)?;d.view_format_vk=view.format.as_raw() as u32;
                d.view_level=view.level.unwrap_or(0);d.view_layer=0;
                d.range_offset=r.offset;d.range_length=span;
            },
            WriteSource::CombinedImageSampler{image:ImageSource::Texture{slot,view},
             sampler:SamplerSource::Slot{slot:s,compare,integer,..}}=>{
                if view.twin||view.level.unwrap_or(0)!=0{return Err("compute combined texture view beyond level0/twin is not expressible".into());}
                if !view.shader_swizzle&&view.components!=vk::ComponentMapping::IDENTITY{return Err("compute combined view component mapping is not expressible".into());}
                if w.ty!=vk::DescriptorType::COMBINED_IMAGE_SAMPLER{return Err("combined compute descriptor has an unexpected type".into());}
                if slot.stage!=Stage::Kernel||s.stage!=Stage::Kernel||s.class!=SlotClass::Sampler{
                    return Err("compute combined descriptor slots are invalid".into());}
                let r=find_resource(2,slot.index).ok_or("compute combined texture is not bound")?;
                let _sampler=find_resource(3,s.index).ok_or("compute combined sampler is not bound")?;
                if r.access&TEGU_AIR_COMPUTE_ACCESS_SAMPLE==0{return Err("combined compute texture is not sampleable".into());}
                let span=compute_texture_span(r).ok_or("compute combined texture span overflows")?;
                if r.offset>r.length||span>r.length-r.offset{
                    return Err("compute combined texture exceeds its retained allocation".into());}
                d.kind=KIND_COMBINED;d.metal_index=slot.index;d.sampler_index=s.index;
                d.sampler_flags=(u32::from(*compare))|(u32::from(*integer)<<1);
                d.flags=slot_flags(*slot)?|TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE;
                d.view_format=metal_view_format(view.format)?;d.view_format_vk=view.format.as_raw() as u32;
                d.view_level=view.level.unwrap_or(0);d.view_layer=0;
                d.range_offset=r.offset;d.range_length=span;
            },
            WriteSource::Sampler{slot,compare,integer,..} if slot.class==SlotClass::Sampler=>{
                if w.ty!=vk::DescriptorType::SAMPLER{return Err("compute sampler descriptor has an unexpected type".into());}
                if slot.stage!=Stage::Kernel{return Err("compute sampler slot stage is invalid".into());}
                find_resource(3,slot.index).ok_or("compute sampler is not bound")?;
                d.kind=KIND_SAMPLER;d.metal_index=slot.index;
                d.sampler_flags=(u32::from(*compare))|(u32::from(*integer)<<1);
                d.flags=0;
            },
            _=>return Err("compute descriptor requires an unsupported resource class".into()),
        }
        out.descriptors[i]=d;
    }
    out.descriptor_count=writes.len() as u32;
    if commands.descriptor_ranges.len()!=out.descriptors.iter().take(writes.len())
        .filter(|d|d.kind==KIND_BUFFER_UNIFORM||d.kind==KIND_BUFFER_STORAGE).count(){
        return Err("compute plan has an unhandled buffer descriptor range".into());
    }
    for u in &plan.uses {
        let UseTarget::Slot(slot)=u.resource else {return Err("compute plan has a non-slot resource use".into());};
        let found=out.descriptors.iter().take(writes.len()).any(|d|match slot.class {
            SlotClass::Buffer=>d.metal_index==slot.index&&(d.kind==KIND_BUFFER_UNIFORM||d.kind==KIND_BUFFER_STORAGE),
            SlotClass::Texture=>d.metal_index==slot.index&&(d.kind==KIND_TEXTURE||d.kind==KIND_IMAGE||d.kind==KIND_COMBINED),
            SlotClass::Sampler=>(d.sampler_index==slot.index&&d.kind==KIND_COMBINED)||(d.metal_index==slot.index&&d.kind==KIND_SAMPLER),
        });
        if !found{return Err("compute plan uses a resource with no matching descriptor".into());}
    }
    for (i,region) in commands.regions.iter().enumerate() {
        if region.indirect.is_some(){return Err("compute indirect dispatch is not supported".into());}
        if !region.stage_flags.is_empty(){return Err("compute region requires unsupported Vulkan stage flags".into());}
        if region.required_subgroup_size.is_some(){return Err("compute region pins a subgroup size; unsupported".into());}
        let mut exact_local=local;
        for (id,axis) in &local_specs {
            let entry=region.specialization.entries.iter().find(|e|e.constant_id==*id)
                .ok_or("compute region does not specialize its local size")?;
            if entry.size!=4{return Err("compute region local size specialization is not u32".into());}
            let start=entry.offset as usize;
            let bytes=region.specialization.data.get(start..start.saturating_add(4))
                .ok_or("compute region local size specialization is short")?;
            let raw:[u8;4]=bytes.try_into().map_err(|_|"compute region local size width")?;
            exact_local[*axis]=u32::from_le_bytes(raw);
        }
        if exact_local.iter().any(|&v|v==0||v>1024){return Err("compute region local size is invalid".into());}
        let mut r=ComputeRegion::default();r.groups=region.group_count;r.local=exact_local;
        if r.groups.iter().any(|&g|g==0||g>65536){return Err("compute region group count is invalid".into());}
        let mut written=[false;128];
        for push in &region.push_constants {
            let start=push.offset as usize;
            let end=start.checked_add(push.bytes.len()).ok_or("compute push range overflow")?;
            if push.stages!=vk::ShaderStageFlags::COMPUTE||end>128{return Err("compute push range exceeds ABI".into());}
            for (k,&byte) in push.bytes.iter().enumerate() {
                let at=start+k;
                if written[at]&&r.push[at]!=byte{return Err("conflicting compute push writes".into());}
                r.push[at]=byte;written[at]=true;
            }
        }
        if region.specialization.entries.len()>64{return Err("too many compute specialization constants".into());}
        for (k,entry) in region.specialization.entries.iter().enumerate() {
            let n=entry.size as usize;let start=entry.offset as usize;
            let end=start.checked_add(n).ok_or("compute specialization overflow")?;
            if !matches!(n,1|2|4|8)||end>region.specialization.data.len()||
               r.constants[..k].iter().any(|c|c.id==entry.constant_id){
                return Err("invalid compute specialization constant".into());}
            r.constants[k].id=entry.constant_id;r.constants[k].size=n as u32;
            r.constants[k].data[..n].copy_from_slice(&region.specialization.data[start..end]);
        }
        r.constant_count=region.specialization.entries.len() as u32;
        out.regions[i]=r;
    }
    if let Some(bytes)=&variant_spirv {
        if bytes.is_empty(){return Err("compute variant has no SPIR-V".into());}
    }
    if let Some(hash)=variant_hash{out.variant_hash=hash;}
    Ok((out,variant_spirv,variant_hash))
}
unsafe fn native_dispatch_v2_impl(pipeline:*const Pipeline,resources:*const ComputeResource,
 count:usize,lengths:*const u32,mode:u32,extents:*const u32,local:*const u32,
 variant_spirv:*mut Blob,output:*mut NativeDispatchV2,error:*mut c_char,cap:usize)->i32 {
    if output.is_null()||variant_spirv.is_null()||output.cast::<u8>()==variant_spirv.cast::<u8>(){return -1;}
    unsafe {*output=NativeDispatchV2::default();*variant_spirv=Blob{bytes:ptr::null_mut(),length:0};}
    let result=unsafe {attempt(error,cap,|| {
        if pipeline.is_null()||extents.is_null()||local.is_null()||count>32||(count>0&&resources.is_null()){
            return Err("invalid compute dispatch v2 arguments".into());}
        let resources=if count==0{&[][..]}else{slice::from_raw_parts(resources,count)};
        let e=[*extents,*extents.add(1),*extents.add(2)];
        let l=[*local,*local.add(1),*local.add(2)];
        let (out,variant,_)=compute_native_dispatch(&*pipeline,resources,lengths,mode,e,l)?;
        Ok((out,variant))
    })};
    match result {
        Some((out,variant))=>{
            unsafe {*output=out;}
            if let Some(bytes)=variant {
                if !bytes.is_empty(){unsafe {*variant_spirv=blob(bytes)};}
            }
            0
        },
        None=>-1,
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_native_dispatch_v2(pipeline:*const Pipeline,
 resources:*const ComputeResource,count:usize,mode:u32,extents:*const u32,local:*const u32,
 variant_spirv:*mut Blob,output:*mut NativeDispatchV2,error:*mut c_char,cap:usize)->i32 {
    unsafe {native_dispatch_v2_impl(pipeline,resources,count,ptr::null(),mode,extents,local,
        variant_spirv,output,error,cap)}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_native_dispatch_v2_tg(pipeline:*const Pipeline,
 resources:*const ComputeResource,count:usize,lengths:*const u32,mode:u32,extents:*const u32,
 local:*const u32,variant_spirv:*mut Blob,output:*mut NativeDispatchV2,error:*mut c_char,cap:usize)->i32 {
    unsafe {native_dispatch_v2_impl(pipeline,resources,count,lengths,mode,extents,local,
        variant_spirv,output,error,cap)}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_blob_free(value:*mut Blob) {
    if value.is_null() {return;}
    let b=unsafe {&mut *value};
    if !b.bytes.is_null() {drop(unsafe {Box::from_raw(ptr::slice_from_raw_parts_mut(b.bytes,b.length))});}
    b.bytes=ptr::null_mut();b.length=0;
}
// air-compute-opus: the m2v metadata of a compute dispatch VARIANT (AIR-only compute codegen,
// runtime_pipeline.inc tegu_compute_compile): the variant whose SPIR-V the last
// tegu_air_pipeline_native_dispatch_v2[_tg] returned (`spirv`, compared word for word with the
// cached variants), serialized exactly as tegu_air_pipeline_create_ex serializes a creation compile.
// 0 ok (*output owned by the caller, tegu_air_blob_free), 1 no cached variant has these words
// (evicted or never planned: the caller refuses), -1 invalid arguments or serialization failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tegu_air_pipeline_variant_metadata(pipeline:*const Pipeline,spirv:*const u8,length:usize,
 output:*mut Blob)->i32 {
    if pipeline.is_null()||spirv.is_null()||output.is_null()||length==0||length%4!=0 {return -1;}
    unsafe {*output=Blob{bytes:ptr::null_mut(),length:0};}
    let r=catch_unwind(AssertUnwindSafe(|| -> Result<Option<Vec<u8>>,String> {
        let p=unsafe {&*pipeline};
        let bytes=unsafe {slice::from_raw_parts(spirv,length)};
        let cache=p.variants.lock().map_err(|_|"compute variant cache poisoned".to_string())?;
        let Some((key,variant))=cache.iter().find(|(_,v)|v.stage.spirv.len()*4==length&&
            v.stage.spirv.iter().zip(bytes.chunks_exact(4)).all(|(w,b)|w.to_le_bytes()==*b)) else {return Ok(None)};
        let stage=&variant.stage;
        let validation=p.device.profile().spirv_validation();
        let meta=serde_json::json!({"schema":1,"validation":{"target_envs":validation.target_envs,"relaxations":validation.relaxations},"profile":variant.profile,"entry_point":stage.entry_point,
            "specialization":stage.specialization,"required_subgroup_size":stage.required_subgroup_size,
            "stage_flags":stage.stage_flags,"precision_notes":stage.precision_notes,
            "reconvergence":stage.reconvergence,"constant_blob":stage.constant_blob.as_deref(),
            "layout":variant.layout,"interface":variant.interface,"dependencies":variant.dependencies,
            "metal_info":variant.metal_info,"variant_bits":variant.variant_bits,
            "creation_compile":{"kind":"dispatch_variant","dispatchable":true,"variant":key}});
        serde_json::to_vec(&meta).map(Some).map_err(|e|e.to_string())
    }));
    match r {Ok(Ok(Some(v)))=>{unsafe {*output=blob(v)};0},Ok(Ok(None))=>1,_=>-1}
}


#[cfg(test)]
mod compute_v2_tests {
    use super::*;
    use std::ffi::c_char;
    use std::path::Path;

    fn fixture(name:&str)->Option<Vec<u8>> {
        let dir=std::env::var("TEGU_COMPUTE_FIXTURES").ok()?;
        std::fs::read(Path::new(&dir).join(name)).ok()
    }
    fn profile()->Option<Vec<u8>>{fixture("profile.json")}
    fn library(air:&[u8])->Library {Library::parse(air).unwrap()}
    fn device(profile:&[u8])->Device {
        let d=DeviceDescription::from_khronos_json(std::str::from_utf8(profile).unwrap(),None).unwrap();
        Device::new(d,DevicePolicy::default()).unwrap()
    }
    fn buffer(index:u32,length:u64)->ComputeResource {
        ComputeResource{version:1,kind:1,index,access:TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_WRITE,
            format:0,texture_type:0,width:0,height:0,depth:0,array_length:0,mipmap_levels:0,sample_count:0,
            usage:0,row_stride:0,storage_mode:0,swizzle_word:0,slice_stride:0,layer_stride:0,array_layers:1,reserved:0,
            min_filter:0,mag_filter:0,mip_filter:0,address_s:0,address_t:0,address_r:0,border_color:0,
            normalized_coordinates:0,lod_average:0,max_anisotropy:0,compare_function:0,reduction_mode:0,
            lod_min_clamp:0.0,lod_max_clamp:0.0,length,offset:0,resource_id:0,swizzle_valid:0,reserved2:0,level_offset:[0;16],level_row_stride:[0;16],level_slice_stride:[0;16],level_layer_stride:[0;16]}
    }
    fn texture(index:u32,format:u32,usage:u32,access:u32,row_stride:u32)->ComputeResource {
        ComputeResource{version:1,kind:2,index,access,format,texture_type:2,width:16,height:16,depth:1,
            array_length:1,mipmap_levels:1,sample_count:1,usage,row_stride,storage_mode:0,swizzle_word:0,slice_stride:0,layer_stride:0,array_layers:1,reserved:0,
            min_filter:0,mag_filter:0,mip_filter:0,address_s:0,address_t:0,address_r:0,border_color:0,
            normalized_coordinates:0,lod_average:0,max_anisotropy:0,compare_function:0,reduction_mode:0,
            lod_min_clamp:0.0,lod_max_clamp:0.0,length:65536,offset:0,resource_id:0x1000+u64::from(index),swizzle_valid:0,reserved2:0,level_offset:[0;16],level_row_stride:[0;16],level_slice_stride:[0;16],level_layer_stride:[0;16]}
    }
    fn sampler(index:u32)->ComputeResource {
        ComputeResource{version:1,kind:3,index,access:0,format:0,texture_type:0,width:0,height:0,depth:0,
            array_length:0,mipmap_levels:0,sample_count:0,usage:0,row_stride:0,storage_mode:0,swizzle_word:0,slice_stride:0,layer_stride:0,array_layers:1,reserved:0,
            min_filter:0,mag_filter:0,mip_filter:0,address_s:0,address_t:0,address_r:0,border_color:0,
            normalized_coordinates:1,lod_average:0,max_anisotropy:1,compare_function:0,reduction_mode:0,
            lod_min_clamp:0.0,lod_max_clamp:f32::MAX,length:0,offset:0,resource_id:0,swizzle_valid:0,reserved2:0,level_offset:[0;16],level_row_stride:[0;16],level_slice_stride:[0;16],level_layer_stride:[0;16]}
    }
    #[test]
    fn producer_explicit_zero_swizzle_is_accepted() {
        let mut r=texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,64);
        r.swizzle_valid=1;r.swizzle_word=0;
        assert!(compute_texture_facts(&r).is_ok());
        r.swizzle_valid=2;
        assert!(compute_texture_facts(&r).is_err());
    }

    #[test]
    fn contiguous_mip_levels_use_exact_facts_and_reject_noncontiguous() {
        let mut r=texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,64);
        r.mipmap_levels=2;
        r.level_offset[0]=0;r.level_offset[1]=64*16;
        r.level_row_stride[0]=64;r.level_row_stride[1]=64;
        assert_eq!(compute_texture_span(&r),Some(64*16+64*8));
        assert!(compute_texture_facts(&r).is_ok());
        r.level_offset[1]=64*16-64;
        assert_eq!(compute_texture_span(&r),None);
        assert!(compute_texture_facts(&r).is_err());
    }

    #[test]
    fn zero_constant_specialization_returns_an_owned_clone() {
        let (Some(air),Some(profile))=(fixture("test_fast_math.air"),profile()) else {
            eprintln!("SKIP zero_constant_specialization_returns_an_owned_clone: TEGU_COMPUTE_FIXTURES unset");
            return;
        };
        let _=profile;
        let library=Library::parse(&air).unwrap();
        let base=Box::into_raw(Box::new(library));
        let mut error=[0 as c_char;256];
        let name=b"TestFastMath";
        let clone=unsafe{tegu_air_library_specialize(base,name.as_ptr(),name.len(),
            std::ptr::null(),0,error.as_mut_ptr(),error.len())};
        assert!(!clone.is_null(),"zero-constant specialization refused: {}",
            unsafe{std::ffi::CStr::from_ptr(error.as_ptr())}.to_string_lossy());
        assert_ne!(base as usize,clone as usize,"an owned clone must not alias the base handle");
        unsafe{tegu_air_library_close(clone);tegu_air_library_close(base);}
    }

    #[test]
    fn abi_layout_is_the_c_manifest() {
        println!("RUST_ABI r={} d={} reg={} v={} vh={} desc={} regions={} reg_flags={} reg_cc={} reg_const={} reg_push={}",
            size_of::<ComputeResource>(),size_of::<ComputeDescriptor>(),size_of::<ComputeRegion>(),
            size_of::<NativeDispatchV2>(),std::mem::offset_of!(NativeDispatchV2,variant_hash),
            std::mem::offset_of!(NativeDispatchV2,descriptors),
            std::mem::offset_of!(NativeDispatchV2,regions),
            std::mem::offset_of!(ComputeRegion,flags),std::mem::offset_of!(ComputeRegion,constant_count),
            std::mem::offset_of!(ComputeRegion,constants),std::mem::offset_of!(ComputeRegion,push));
        assert_eq!(size_of::<ComputeResource>(),488);
        assert_eq!(size_of::<ComputeDescriptor>(),64);
        assert_eq!(size_of::<ComputeRegion>(),1184);
        assert_eq!(size_of::<NativeDispatchV2>(),11592);
    }
    #[test]
    fn threadgroup_lengths_are_sparse_and_validated() {
        let mut table=StageTable::default();
        let mut lengths=[0u32;31];
        lengths[0]=16;lengths[2]=256;lengths[30]=32768;
        unsafe {apply_threadgroup_lengths(&mut table,lengths.as_ptr()).unwrap();}
        assert_eq!(table.threadgroup_lengths.get(&0),Some(&16));
        assert_eq!(table.threadgroup_lengths.get(&1),None);
        assert_eq!(table.threadgroup_lengths.get(&2),Some(&256));
        assert_eq!(table.threadgroup_lengths.get(&30),Some(&32768));
        for bad in [1u32,15,17,32784] {
            lengths[0]=bad;
            let mut table=StageTable::default();
            assert!(unsafe{apply_threadgroup_lengths(&mut table,lengths.as_ptr())}.is_err(),
                "length {bad} must be refused");
        }
        let mut table=StageTable::default();
        unsafe {apply_threadgroup_lengths(&mut table,ptr::null()).unwrap();}
        assert!(table.threadgroup_lengths.is_empty());
    }
    #[test]
    fn rejects_duplicate_and_mismatched_resources() {
        assert!(compute_state(&[buffer(0,64),buffer(0,64)],ptr::null()).is_err());
        let mut t=texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,64);
        t.resource_id=0;assert!(compute_state(&[t],ptr::null()).is_err());
        let mut t=texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_STORAGE|TEGU_AIR_COMPUTE_ACCESS_WRITE,64);
        assert!(compute_state(&[t],ptr::null()).is_err());
        t.usage=2;assert!(compute_state(&[t],ptr::null()).is_ok());
        let mut s=sampler(0);s.reduction_mode=1;
        assert!(compute_state(&[s],ptr::null()).is_ok()); // translator decides representability at plan time
        s.index=16;assert!(compute_state(&[s],ptr::null()).is_err());
    }
    #[test]
    fn real_sampled_kernel_plans_complete_resources() {
        let (Some(air),Some(profile))=(fixture("test_fast_math.air"),profile()) else {
            eprintln!("SKIP real_sampled_kernel_plans_complete_resources: TEGU_COMPUTE_FIXTURES unset");
            return;
        };
        let lib=library(&air);
        let d=device(&profile);
        let f=lib.descriptor("TestFastMath").unwrap();
        let ((_,pipeline),_,creation_only)=compile_compute_creation(&d,&f,&Budget::standard()).unwrap();
        assert!(!creation_only);
        let p=Pipeline{device:d,pipeline,creation_only,function:f,variants:std::sync::Mutex::new(Vec::new()),extra:ComputeExtra::default()};
        let resources=[buffer(0,4096),texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,64),sampler(0)];
        let (out,variant,_)=compute_native_dispatch(&p,&resources,ptr::null(),TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS,[4,1,1],[4,1,1]).unwrap();
        assert!(variant.is_none());
        assert_eq!(out.descriptor_count,2);
        assert_eq!(out.region_count,1);
        assert_eq!(out.descriptors[0].binding,0);
        assert_eq!(out.descriptors[0].kind,KIND_BUFFER_STORAGE);
        assert_eq!(out.descriptors[0].flags&TEGU_AIR_COMPUTE_ACCESS_WRITE,TEGU_AIR_COMPUTE_ACCESS_WRITE);
        assert_eq!(out.descriptors[1].binding,1);
        assert_eq!(out.descriptors[1].kind,KIND_COMBINED);
        assert_eq!((out.descriptors[1].metal_index,out.descriptors[1].sampler_index),(0,0));
        assert_eq!(out.descriptors[1].range_offset,0);
        assert_eq!(out.descriptors[1].range_length,64*16);
        assert_eq!(out.regions[0].groups,[4,1,1]);
        assert!(out.regions[0].constant_count>=4);
        assert!(out.regions[0].constants.iter().take(out.regions[0].constant_count as usize).any(|c|c.id==4));
        assert!(out.regions[0].push[..36].iter().any(|&b|b!=0));
        // The sampler's word is a specialization input, not a fabricated default.
        let sampler_constant=out.regions[0].constants.iter()
            .take(out.regions[0].constant_count as usize).find(|c|c.id==4).unwrap();
        assert_eq!(sampler_constant.size,4);
        assert!(compute_native_dispatch(&p,&[resources[0].clone()],ptr::null(),TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS,[1,1,1],[4,1,1]).is_err());
    }
    #[test]
    fn real_sampled_kernel_plans_exact_threads_regions() {
        let (Some(air),Some(profile))=(fixture("test_fast_math.air"),profile()) else {
            eprintln!("SKIP real_sampled_kernel_plans_exact_threads_regions: TEGU_COMPUTE_FIXTURES unset");
            return;
        };
        let lib=library(&air);let d=device(&profile);
        let f=lib.descriptor("TestFastMath").unwrap();
        let ((_,pipeline),_,creation_only)=compile_compute_creation(&d,&f,&Budget::standard()).unwrap();
        let p=Pipeline{device:d,pipeline,creation_only,function:f,variants:std::sync::Mutex::new(Vec::new()),extra:ComputeExtra::default()};
        let resources=[buffer(0,4096),texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,64),sampler(0)];
        let (out,_,_)=compute_native_dispatch(&p,&resources,ptr::null(),TEGU_AIR_COMPUTE_DISPATCH_THREADS,[17,9,1],[8,4,1]).unwrap();
        assert!(out.region_count>=2,"non-uniform 17x9 grid must not be one padded threadgroup region");
        assert!(out.regions.iter().take(out.region_count as usize).all(|r|r.groups.iter().all(|&g|g>0)));
        assert!(out.regions.iter().take(out.region_count as usize).any(|r|r.groups[0]<=3&&r.groups[1]<=3));
        let mut local_ids=[None;3];
        for c in &p.pipeline.stage.specialization.constants {
            if let metal2vulkan::pipeline::SpecKey::LocalSize(axis)=c.key {
                local_ids[axis as usize]=Some(c.id);
            }
        }
        assert!(local_ids.iter().all(Option::is_some));
        let mut locals=Vec::new();
        for region in out.regions.iter().take(out.region_count as usize) {
            let mut local=[0u32;3];
            for (axis,id) in local_ids.iter().enumerate() {
                let c=region.constants.iter().take(region.constant_count as usize)
                    .find(|c|Some(c.id)==*id).unwrap();
                assert_eq!(c.size,4);
                local[axis]=u32::from_le_bytes(c.data[..4].try_into().unwrap());
            }
            assert_eq!(region.local,local,"region local must equal its specialized LocalSize constants");
            locals.push(local);
        }
        for want in [[8,4,1],[8,1,1],[1,4,1],[1,1,1]] {
            assert!(locals.contains(&want),"missing exact edge-region local {want:?}");
        }
        // Every region carries its own complete specialization, including the
        // local size and the MultiRegion constant.
        for region in out.regions.iter().take(out.region_count as usize) {
            assert!(region.constant_count>=4);
            assert!(region.constants.iter().take(region.constant_count as usize).any(|c|c.id==3));
        }
    }
    #[test]
    fn real_sampled_kernel_plans_two_contiguous_mip_levels() {
        let (Some(air),Some(profile))=(fixture("test_fast_math.air"),profile()) else {
            eprintln!("SKIP real_sampled_kernel_plans_two_contiguous_mip_levels: TEGU_COMPUTE_FIXTURES unset");
            return;
        };
        let lib=library(&air);let d=device(&profile);
        let f=lib.descriptor("TestFastMath").unwrap();
        let ((_,pipeline),_,creation_only)=compile_compute_creation(&d,&f,&Budget::standard()).unwrap();
        let p=Pipeline{device:d,pipeline,creation_only,function:f,variants:std::sync::Mutex::new(Vec::new()),extra:ComputeExtra::default()};
        let mut t=texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,64);
        t.mipmap_levels=2;t.level_offset[0]=0;t.level_offset[1]=64*16;
        t.level_row_stride[0]=64;t.level_row_stride[1]=64;
        let resources=[buffer(0,4096),t,sampler(0)];
        let (out,_,_)=compute_native_dispatch(&p,&resources,ptr::null(),TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS,[1,1,1],[4,4,1]).unwrap();
        let d=out.descriptors.iter().take(out.descriptor_count as usize)
            .find(|d|d.kind==KIND_COMBINED).unwrap();
        assert_eq!(d.range_length,64*16+64*8,"producer span must cover both levels");
    }

    #[test]
    fn real_storage_image_kernel_requires_a_bound_format_variant() {
        let (Some(air),Some(profile))=(fixture("accumulate_kernel.air"),profile()) else {
            eprintln!("SKIP real_storage_image_kernel_requires_a_bound_format_variant: TEGU_COMPUTE_FIXTURES unset");
            return;
        };
        let lib=library(&air);let d=device(&profile);
        let f=lib.descriptor("accumulateKernel").unwrap();
        let ((_,pipeline),_,creation_only)=compile_compute_creation(&d,&f,&Budget::standard()).unwrap();
        let p=Pipeline{device:d,pipeline,creation_only,function:f,variants:std::sync::Mutex::new(Vec::new()),extra:ComputeExtra::default()};
        let resources=[buffer(0,4096),
            texture(0,80,1,TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE,64),
            texture(1,115,2,TEGU_AIR_COMPUTE_ACCESS_STORAGE|TEGU_AIR_COMPUTE_ACCESS_WRITE,128)];
        let (out,variant,_)=compute_native_dispatch(&p,&resources,ptr::null(),TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS,[4,1,1],[4,1,1]).unwrap();
        assert_eq!(out.descriptor_count,3);
        assert_eq!(out.descriptors[2].kind,KIND_IMAGE);
        assert_eq!(out.descriptors[2].flags&TEGU_AIR_COMPUTE_ACCESS_WRITE,TEGU_AIR_COMPUTE_ACCESS_WRITE);
        assert!(out.descriptors[2].range_length>0);
        if creation_only {assert!(variant.is_some(),"stand-in pipeline must return the bound-format SPIR-V");}
        assert!(variant.as_ref().is_none_or(|v|!v.is_empty()));
    }
}
#[cfg(test)]
mod render_push_tests {
    use super::*;
    use metal2vulkan::pipeline::{PushPlacement, PushLoc, PushField};
    fn placement() -> PushPlacement {
        serde_json::from_str(r#"{"stage":"Fragment","argument_table_size":0,
            "push_range":[64,4],"fields":[{"field":{"ColorWriteRule":0},
            "loc":{"Push":{"offset":64}},"size":4}]}"#).unwrap()
    }
    #[test]
    fn uses_reflected_offset_and_rule_not_fixture_constants() {
        let mut p=placement();
        p.fields[0].loc=PushLoc::Push{offset:80};p.push_range=Some((64,20));
        p.fields[0].field=PushField::ColorWriteRule(3);
        let out=render_push(&[p],&[(3,0x12345678)]).unwrap();
        assert_eq!(out.fragment_span,84);
        let mut expected=[0;128];expected[80..84].copy_from_slice(&0x12345678u32.to_le_bytes());
        assert_eq!(out.fragment,expected);assert_eq!(out.vertex,[0;128]);
    }
    #[test]
    fn refuses_unresolved_and_invalid_placements() {
        let rules=[(0,0x2222)];
        assert!(render_push(&[placement()],&[]).is_err());
        assert!(render_push(&[placement()],&[(0,1),(0,2)]).is_err());
        assert!(render_push(&[placement(),placement()],&rules).is_err());
        let mut p=placement();p.fields.push(p.fields[0]);
        assert!(render_push(&[p],&rules).is_err());
        for (offset,size) in [(0,4),(65,4),(128,4),(u32::MAX,4),(64,8),(64,0)] {
            let mut p=placement();p.fields[0].loc=PushLoc::Push{offset};p.fields[0].size=size;
            assert!(render_push(&[p],&rules).is_err());
        }
        for range in [None,Some((64,0)),Some((64,65)),Some((u32::MAX,2))] {
            let mut p=placement();p.push_range=range;
            assert!(render_push(&[p],&rules).is_err());
        }
        let mut p=placement();p.argument_table_size=16;
        assert!(render_push(&[p],&rules).is_err());
        let mut p=placement();p.fields[0].loc=PushLoc::ArgumentTable{offset:0};
        assert!(render_push(&[p],&rules).is_err());
        let mut p=placement();p.stage=Stage::Vertex;
        assert!(render_push(&[p],&rules).is_err());
        let mut p=placement();p.stage=Stage::Kernel;
        assert!(render_push(&[p],&rules).is_err());
    }
}


#[cfg(test)]
mod texture_contract_tests {
    use super::*;
    #[test]
    fn bridge_owned_contract_parses_and_note_is_off_at_default() {
        let c=texture_contract().unwrap();
        assert_eq!((c.slots.len(),c.dependencies.len(),c.constants.len(),c.specialized.len()),(2,6,1,1));
        let note=&c.specialized[0];
        assert!(!specialized_holds_at_default(note,&c.constants));
        // Pixel coordinates (bit0) + linear min (bit1), nearest mag: holds.
        let mut pixel=c.constants.clone();pixel[0].default=0b011;
        assert!(specialized_holds_at_default(note,&pixel));
        pixel[0].default=0b111;
        assert!(!specialized_holds_at_default(note,&pixel));
        assert!(specialized_holds_at_default(note,&[]));
    }
}

#[cfg(test)]
mod typed_render_usage_tests {
    use super::*;

    fn one() -> RenderTypedPlan {
        let mut plan=RenderTypedPlan::default();plan.version=1;plan
    }
    fn use_at(plan:&RenderTypedPlan,index:usize)->RenderResourceUse {plan.uses[index]}

    #[test]
    fn whole_range_dominates_bounded_for_every_order() {
        for order in [
            [Some((8,4)),None,Some((32,2))],
            [None,Some((8,4)),Some((32,2))],
            [Some((8,4)),Some((32,2)),None],
        ] {
            let mut plan=one();
            for range in order {typed_usage_add(&mut plan,1,1,2,2,1,range).unwrap();}
            let u=use_at(&plan,0);
            assert_eq!(plan.count,1);assert_eq!(u.flags&36,32);
            assert_eq!((u.range_offset,u.range_length),(0,0));
        }
    }

    #[test]
    fn disjoint_ranges_union_and_overflow_refuses_without_mutation() {
        let mut plan=one();
        typed_usage_add(&mut plan,1,2,6,2,1,Some((16,4))).unwrap();
        typed_usage_add(&mut plan,1,2,6,2,1,Some((40,2))).unwrap();
        let u=use_at(&plan,0);assert_eq!((u.range_offset,u.range_length),(16,26));
        let before=plan;
        assert!(typed_usage_add(&mut plan,1,2,6,2,1,Some((u64::MAX,1))).is_err());
        assert_eq!(plan.count,before.count);assert_eq!(plan.uses[0],before.uses[0]);
    }

    #[test]
    fn duplicate_key_preserves_all_access_classes() {
        let mut plan=one();
        typed_usage_add(&mut plan,1,1,2,2,1,Some((0,16))).unwrap();
        typed_usage_add(&mut plan,1,1,2,5,1,Some((8,16))).unwrap();
        let u=use_at(&plan,0);assert_eq!(u.class_mask,(1<<1)|(1<<4));
        assert_eq!((u.range_offset,u.range_length),(0,24));
    }

    #[test]
    fn unsupported_usage_keys_and_input_kinds_are_rejected() {
        let mut plan=one();
        for (kind,stage,index,class) in [(0,1,0,2),(4,1,0,2),(1,3,0,2),(3,1,16,0),(1,1,31,2),(1,1,0,0)] {
            assert!(typed_usage_add(&mut plan,kind,stage,index,class,1,None).is_err());
            assert_eq!(plan.count,0);
        }
        for (kind,stage) in [(1,3),(99,1)] {
            let mut resource:RenderResource=unsafe{std::mem::zeroed()};
            resource.kind=kind;resource.stage=stage;
            assert!(compositor_plan_inputs(&[resource]).is_err());
        }
    }

    fn sphere_volume() -> RenderResource {
        let mut r:RenderResource=unsafe{std::mem::zeroed()};
        r.kind=2;r.stage=1;r.index=0;r.format=10;r.texture_type=7;
        r.width=64;r.height=64;r.array_length=1;r.mipmap_levels=1;
        r.sample_count=1;r.usage=1;r.swizzle=[2,3,4,5];
        r.address_mode_identity=1;r.resource_id=0x100000;
        r
    }

    #[test]
    fn exact_sphere_volume_needs_no_runtime_sampler() {
        let r=sphere_volume();
        let (state,_)=compositor_plan_inputs(&[r]).unwrap();
        let vertex=state.stages.get(&metal2vulkan::pipeline::Stage::Vertex).unwrap();
        assert!(vertex.textures.contains_key(&0));
        assert!(vertex.samplers.is_empty());
        for mutate in [
            |x:&mut RenderResource|x.format=70,
            |x:&mut RenderResource|x.texture_type=3, /* 2DArray, outside the bounded 2D class */
            |x:&mut RenderResource|x.width=63,
            |x:&mut RenderResource|x.height=63,
            |x:&mut RenderResource|x.mipmap_levels=2,
            |x:&mut RenderResource|x.usage=5,
        ] as [fn(&mut RenderResource);6] {
            let mut bad=sphere_volume();mutate(&mut bad);
            assert!(compositor_plan_inputs(&[bad]).is_err());
        }
    }
}

#[cfg(test)]
mod fixed_ui_r80_resource_tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn admits_only_the_two_exact_bounded_uniform_windows_and_push_fields() {
        let bindings=json!([
            {"binding":0,"count":1,"flags":0,"immutable_sampler":null,"source":{"Buffer":{"class":"Buffer","index":2,"stage":"Vertex"}},"stages":1,"ty":6},
            {"binding":1,"count":1,"flags":0,"immutable_sampler":null,"source":{"Buffer":{"class":"Buffer","index":1,"stage":"Fragment"}},"stages":16,"ty":6},
            {"binding":2,"count":1,"flags":0,"immutable_sampler":null,"source":{"Synthetic":{"binding":"ConstantBlob","stage":"Fragment"}},"stages":16,"ty":7}
        ]);
        let sets=json!([{"bindings":bindings,"flags":0,"set":0}]);
        assert!(fixed_ui_r80_layout_values_match(Some(&sets),&bindings));
        let mut altered=sets.clone();altered[0]["bindings"][1]["count"]=json!(2);
        assert!(!fixed_ui_r80_layout_values_match(Some(&altered),&bindings));
        let mut offsets=vec![68,0,64];assert!(fixed_ui_r80_push_offsets_match(&mut offsets));
        for mut bad in [vec![0,64],vec![0,64,72],vec![0,64,64]] {
            assert!(!fixed_ui_r80_push_offsets_match(&mut bad));
        }
        assert!(fixed_ui_r80_push_field_matches(0,Stage::Vertex,SlotClass::Buffer,2));
        assert!(fixed_ui_r80_push_field_matches(64,Stage::Fragment,SlotClass::Buffer,1));
        assert!(fixed_ui_r80_push_field_matches(68,Stage::Fragment,SlotClass::Buffer,1));
        assert!(!fixed_ui_r80_push_field_matches(68,Stage::Vertex,SlotClass::Buffer,2));
    }

    fn buffer(stage:u32,index:u32,length:u64,offset:u64)->RenderResource {
        let mut r:RenderResource=unsafe{std::mem::zeroed()};
        r.kind=1;r.stage=stage;r.index=index;r.length=length;r.offset=offset;r
    }

    #[test]
    fn exact_retained_buffers_are_order_independent_and_bounded() {
        let vs=buffer(1,2,512,32);let fs=buffer(2,1,512,64);
        let resources=[fs,vs];let exact=fixed_ui_r80_input_resources(&resources).unwrap();
        assert_eq!((exact[0].stage,exact[0].index,exact[1].stage,exact[1].index),(1,2,2,1));
        assert!(compositor_plan_inputs(&resources).is_ok());
        let mut tokenized=vs;tokenized.resource_id=1;
        assert!(compositor_plan_inputs(&[tokenized,fs]).is_err());
        for bad in [
            vec![vs],
            vec![vs,buffer(2,1,200,64)],
            vec![buffer(1,2,95,32),fs],
            vec![vs,buffer(2,1,231,64)],
            vec![vs,fs,buffer(1,0,64,0)],
        ] {assert!(fixed_ui_r80_input_resources(&bad).is_err());}
    }

    #[test]
    fn fixed_ui_r80_c_layout_sizes_are_stable() {
        assert_eq!(std::mem::size_of::<FixedUiR80Binding>(),48);
        assert_eq!(std::mem::size_of::<FixedUiR80PushSpan>(),16);
        assert_eq!(std::mem::size_of::<FixedUiR80Plan>(),184);
    }

    #[test]
    fn resolved_push_projection_is_byte_exact_and_refuses_auxiliary_writes() {
        let vertex=[1,2,3,4];let fragment=[5,6,7,8,9,10,11,12];
        let mut out=FixedUiR80Plan::default();
        fixed_ui_r80_pack_pushes([(16,64,fragment.as_slice()),(1,0,vertex.as_slice())],&mut out).unwrap();
        assert_eq!((out.push_count,out.push_bytes_length),(2,12));
        assert_eq!(out.pushes,[
            FixedUiR80PushSpan{stage:1,offset:0,length:4,bytes_offset:0},
            FixedUiR80PushSpan{stage:16,offset:64,length:8,bytes_offset:4},
        ]);
        assert_eq!(out.push_bytes,[1,2,3,4,5,6,7,8,9,10,11,12]);
        for writes in [
            vec![(1,0,vertex.as_slice())],
            vec![(1,0,vertex.as_slice()),(16,64,vertex.as_slice())],
            vec![(1,4,vertex.as_slice()),(16,64,fragment.as_slice())],
            vec![(1,0,vertex.as_slice()),(16,64,fragment.as_slice()),(1,8,vertex.as_slice())],
        ] {assert!(fixed_ui_r80_pack_pushes(writes,&mut FixedUiR80Plan::default()).is_err());}
    }
}

#[cfg(test)]
mod fixed_ui_r85_texture_tests {
    use super::*;

    fn resource(kind:u32,stage:u32,index:u32)->RenderResource {
        let mut r:RenderResource=unsafe{std::mem::zeroed()};r.kind=kind;r.stage=stage;r.index=index;r
    }
    fn buffer(stage:u32,index:u32,length:u64,offset:u64)->RenderResource {
        let mut r=resource(1,stage,index);r.length=length;r.offset=offset;r
    }
    fn texture()->RenderResource {
        let mut r=resource(2,2,3);r.format=70;r.texture_type=2;r.width=64;r.height=32;
        r.array_length=1;r.mipmap_levels=1;r.sample_count=1;r.usage=1;r.swizzle=[2,3,4,5];
        r.address_mode_identity=1;r.resource_id=0x1234;r
    }
    fn sampler()->RenderResource {
        let mut r=resource(3,2,0);r.normalized_coordinates=1;r.max_anisotropy=1;r
    }
    fn resources()->[RenderResource;7] {[
        buffer(2,2,64,0),sampler(),buffer(1,1,512,0),texture(),
        buffer(1,3,32,4),buffer(2,1,512,64),buffer(1,2,256,32)
    ]}

    #[test]
    fn exact_seven_resources_are_order_independent_and_bounded() {
        let all=resources();let exact=fixed_ui_r85_input_resources(&all).unwrap();
        assert_eq!(exact.iter().map(|r|(r.kind,r.stage,r.index)).collect::<Vec<_>>(),[
            (1,1,2),(1,1,3),(1,2,1),(1,2,2),(1,1,1),(2,2,3),(3,2,0)]);
        assert!(compositor_plan_inputs(&all).is_ok());
        for (stage,index,too_short) in [(1,2,95),(1,3,47),(2,1,199),(2,2,39)] {
            let mut bad=resources();let r=bad.iter_mut().find(|r|r.kind==1&&r.stage==stage&&r.index==index).unwrap();
            r.offset=32;r.length=too_short;
            assert!(fixed_ui_r85_input_resources(&bad).is_err());
        }
        let mut duplicate=resources().to_vec();duplicate[0]=duplicate[4];
        assert!(fixed_ui_r85_input_resources(&duplicate).is_err());
    }

    #[test]
    fn push_projection_and_metadata_fields_are_exact() {
        for (offset,stage,index) in [(0,Stage::Vertex,2),(4,Stage::Vertex,3),(64,Stage::Fragment,1),(68,Stage::Fragment,2)] {
            assert!(fixed_ui_r85_push_field_matches(offset,stage,SlotClass::Buffer,index));
        }
        assert!(!fixed_ui_r85_push_field_matches(72,Stage::Fragment,SlotClass::Buffer,2));
        let vertex=[1,2,3,4,5,6,7,8];let fragment=[9,10,11,12,13,14,15,16,17,18,19,20];
        let mut out=FixedUiR85Plan::default();
        fixed_ui_r85_pack_pushes([(16,64,fragment.as_slice()),(1,0,vertex.as_slice())],&mut out).unwrap();
        assert_eq!((out.push_count,out.push_bytes_length),(2,20));
        assert_eq!(out.pushes,[
            FixedUiR85PushSpan{stage:1,offset:0,length:8,bytes_offset:0},
            FixedUiR85PushSpan{stage:16,offset:64,length:12,bytes_offset:8},
        ]);
        assert_eq!(out.push_bytes,[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]);
        assert!(fixed_ui_r85_pack_pushes([(1,0,vertex.as_slice())],&mut FixedUiR85Plan::default()).is_err());
    }

    #[test]
    fn fixed_ui_r85_c_layout_sizes_are_stable() {
        assert_eq!(std::mem::size_of::<FixedUiR85Binding>(),48);
        assert_eq!(std::mem::size_of::<FixedUiR85PushSpan>(),16);
        assert_eq!(std::mem::size_of::<FixedUiR85TextureBinding>(),48);
        assert_eq!(std::mem::size_of::<FixedUiR85Plan>(),336);
    }
}

#[cfg(test)]
mod graphics_specialization_tests {
    use super::*;
    use std::path::Path;

    fn render_fixture(name:&str)->Option<Vec<u8>> {
        let dir=std::env::var("TEGU_RENDER_FIXTURES").ok()?;
        std::fs::read(Path::new(&dir).join(name)).ok()
    }
    fn compute_fixture(name:&str)->Option<Vec<u8>> {
        let dir=std::env::var("TEGU_COMPUTE_FIXTURES").ok()?;
        std::fs::read(Path::new(&dir).join(name)).ok()
    }
    fn resource(kind:u32,stage:u32,index:u32)->RenderResource {
        let mut r:RenderResource=unsafe{std::mem::zeroed()};r.kind=kind;r.stage=stage;r.index=index;r
    }
    fn texture(stage:u32,index:u32)->RenderResource {
        let mut r=resource(2,stage,index);r.format=80;r.texture_type=2;r.width=64;r.height=64;
        r.array_length=1;r.mipmap_levels=1;r.sample_count=1;r.usage=1;r.swizzle=[2,3,4,5];
        r.address_mode_identity=1;r.resource_id=0x4000_0000+u64::from(stage)*16+u64::from(index);r
    }
    fn linear_sampler(stage:u32,index:u32)->RenderResource {
        let mut r=resource(3,stage,index);r.min_filter=1;r.mag_filter=1;r.mip_filter=0;
        r.address_s=2;r.address_t=2;r.address_r=2;r.normalized_coordinates=1;r.max_anisotropy=1;r
    }

    #[test]
    fn astc_is_admitted_only_by_the_specialized_path() {
        let mut r=texture(1,0);r.format=204;r.swizzle=[2,3,4,5];
        assert!(compositor_plan_inputs(&[r]).is_err(),"old path must keep its format set");
        assert!(compositor_plan_inputs_mode(&[r],true).is_ok(),"honest ASTC facts must reach the specialized planner");
    }

    #[test]
    fn half2_vertex_format_is_admitted_and_unrelated_formats_refuse() {
        use metal2vulkan::pipeline::{VertexAttribute,VertexBinding,VertexInputState,VertexStride};
        let mut input=VertexInputState::default();
        let mut a:VertexAttribute=unsafe{std::mem::zeroed()};
        a.location=0;a.binding=0;a.format=vk::Format::R16G16_SFLOAT;a.offset=0;
        let mut b:VertexBinding=unsafe{std::mem::zeroed()};
        b.binding=0;b.buffer_index=3;b.input_rate=vk::VertexInputRate::VERTEX;b.divisor=1;
        b.stride=VertexStride::Static(4);
        input.attributes.push(a);input.bindings.push(b);
        let fetch=render_vertex_fetch_input(&input).unwrap();
        assert_eq!(fetch.count,1);
        assert_eq!(fetch.attributes[0].format,83);
        let mut bad=input;
        bad.attributes[0].format=vk::Format::R8G8B8A8_UNORM;
        assert!(render_vertex_fetch_input(&bad).is_err());
    }

    #[test]
    fn bgr5a1_sampled_texture_reaches_the_translator_as_a1r5g5b5() {
        use metal2vulkan::metal::{PixelFormat,format::direct_format};
        // c6hc: QuartzCore samples its BGR5A1 blur targets (565x1220 single
        // level, usage 5, and the 776x24 ten-level pyramid).
        assert_eq!(direct_format(PixelFormat::BGR5A1Unorm),Some(vk::Format::A1R5G5B5_UNORM_PACK16));
        let mut r=texture(2,3);r.format=43;r.width=565;r.height=1220;r.usage=5;
        for allow in [false,true] {
            let (state,_)=compositor_plan_inputs_mode(&[r],allow).unwrap();
            let stage=state.stages.get(&metal2vulkan::pipeline::Stage::Fragment).unwrap();
            match stage.textures.get(&3).unwrap() {
                metal2vulkan::bind::BoundTexture::Texture{facts,..}=>
                    assert_eq!(facts.pixel_format,PixelFormat::BGR5A1Unorm),
                _=>panic!("BGR5A1 texture did not reach the translator table"),
            }
        }
        let mut mips=texture(2,0);mips.format=43;mips.width=776;mips.height=24;mips.mipmap_levels=10;mips.usage=5;
        assert!(compositor_plan_inputs(&[mips]).is_err(),"old path keeps single-level only");
        assert!(compositor_plan_inputs_mode(&[mips],true).is_ok(),"specialized path admits the mip pyramid");
        // Packed 16-bit formats with an arch9 descriptor are sampled on the
        // specialized path; A1BGR5 (no Mali bit layout) and the old path refuse.
        for other in [40u32,42] {
            let mut x=texture(2,0);x.format=other;
            assert!(compositor_plan_inputs_mode(&[x],true).is_ok(),"format {other} must be admitted");
            assert!(compositor_plan_inputs(&[x]).is_err(),"old path keeps its format set");
        }
        let mut a1=texture(2,0);a1.format=41;
        assert!(compositor_plan_inputs_mode(&[a1],true).is_err(),"A1BGR5 must stay refused");
    }

    #[test]
    fn specialized_planner_admits_bounded_array_mips_only_on_new_path() {
        let mut r=texture(2,0);r.texture_type=3;r.array_length=4;r.mipmap_levels=4;
        r.sample_count=1;r.usage=1;r.width=64;r.height=64;
        assert!(compositor_plan_inputs(&[r]).is_err(),"old path must keep its bounded class");
        let (state,_)=compositor_plan_inputs_mode(&[r],true).unwrap();
        let stage=state.stages.get(&metal2vulkan::pipeline::Stage::Fragment).unwrap();
        match stage.textures.get(&0).unwrap() {
            metal2vulkan::bind::BoundTexture::Texture{facts,..}=>{
                assert_eq!(facts.array_length,4);
                assert_eq!(facts.mipmap_level_count,4);
                assert_eq!(facts.texture_type,metal2vulkan::metal::TextureType::Type2DArray);
            },
            _=>panic!("array texture did not reach the translator table"),
        }
    }

    #[test]
    fn real_generic_linear_sampler_is_not_projected_to_nearest() {
        let (Some(air),Some(profile))=(render_fixture("probe.air"),compute_fixture("profile.json")) else {
            eprintln!("SKIP real_generic_linear_sampler_is_not_projected_to_nearest: fixtures unset");
            return;
        };
        use metal2vulkan::{metal::PixelFormat,pipeline::{ColorAttachmentDescriptor,RenderPipelineDescriptor,StageSource}};
        let library=Library::parse(&air).unwrap();
        let description=DeviceDescription::from_khronos_json(std::str::from_utf8(&profile).unwrap(),None).unwrap();
        let device=Device::new(description,DevicePolicy::default()).unwrap();
        let vertex=library.descriptor("vt_vertex").unwrap();
        let fragment=library.descriptor("vt_fragment").unwrap();
        let mut descriptor=RenderPipelineDescriptor::new(StageSource::Function(vertex));
        descriptor.fragment=Some(StageSource::Function(fragment));
        descriptor.color_attachments[0]=Some(ColorAttachmentDescriptor::new(PixelFormat::BGRA8Unorm));
        let pipeline=device.compile_render_pipeline(&descriptor,&Budget::standard()).unwrap();
        let handle=RenderPipelineHandle{_device:device,_pipeline:pipeline,source_blend:None,
            requested_attachments:vec![RenderColorAttachment{format:80,write_mask:15}],
            requested_attachment_blends:None,color_format:80,explicit_targets:false,plan_cache:Default::default(),descriptor:Default::default(),variants:Default::default()};
        let handle=Box::into_raw(Box::new(handle));
        let resources=[texture(1,0),linear_sampler(1,0),texture(2,0),linear_sampler(2,0)];
        let draw=RenderDraw{version:1,vertex_start:0,vertex_count:3,instance_count:1,base_instance:0};
        let mut generic=GenericPlan::default();let mut spec=GraphicsSpecializationPlan::default();
        let mut error=[0 as c_char;512];
        let rc=unsafe{tegu_air_render_pipeline_plan_generic_specialized(handle,resources.as_ptr(),
            resources.len(),ptr::null(),0,&draw,&mut generic,&mut spec,error.as_mut_ptr(),error.len())};
        assert_eq!(rc,0,"linear sampler plan refused: {}",unsafe{std::ffi::CStr::from_ptr(error.as_ptr())}.to_string_lossy());
        let mut old=GenericPlan::default();
        let old_rc=unsafe{tegu_air_render_pipeline_plan_generic(handle,resources.as_ptr(),resources.len(),
            ptr::null(),0,&draw,&mut old,error.as_mut_ptr(),error.len())};
        assert_ne!(old_rc,0,"old default-only entry must refuse real linear sampler facts");
        assert_eq!(generic.binding_count,spec.binding_count);
        assert!(generic.binding_count>=2);
        let image=generic.bindings[..generic.binding_count as usize].iter()
            .find(|b|matches!(b.kind,GENERIC_COMBINED|GENERIC_COMBINED_IMMUTABLE|GENERIC_TEXTURE)).unwrap();
        let sb=spec.bindings[..spec.binding_count as usize].iter().find(|b|b.binding==image.binding).unwrap();
        assert_eq!(sb.view_format_metal,80);
        assert_ne!(sb.view_format_vk,0);
        assert_ne!(sb.view_type,0);
        assert_ne!(sb.aspect,0);
        assert!(spec.specialization_length>0);
        assert!(spec.vertex_count+spec.fragment_count>0);
        let mut mt1=texture(1,0);mt1.mipmap_levels=4;
        let mut mt2=texture(2,0);mt2.mipmap_levels=4;
        let mip_resources=[mt1,linear_sampler(1,0),mt2,linear_sampler(2,0)];
        let mut mip_generic=GenericPlan::default();let mut mip_spec=GraphicsSpecializationPlan::default();
        let mip_rc=unsafe{tegu_air_render_pipeline_plan_generic_specialized(handle,mip_resources.as_ptr(),
            mip_resources.len(),ptr::null(),0,&draw,&mut mip_generic,&mut mip_spec,error.as_mut_ptr(),error.len())};
        assert_eq!(mip_rc,0,"bounded mip chain plan refused: {}",unsafe{std::ffi::CStr::from_ptr(error.as_ptr())}.to_string_lossy());
        assert!(mip_spec.binding_count>=2);
        let mut mip_old=GenericPlan::default();
        assert_ne!(unsafe{tegu_air_render_pipeline_plan_generic(handle,mip_resources.as_ptr(),mip_resources.len(),
            ptr::null(),0,&draw,&mut mip_old,error.as_mut_ptr(),error.len())},0,
            "old default-only entry must refuse a mip chain");
        unsafe{drop(Box::from_raw(handle));}
    }

    fn plan_bytes<T>(v:&T)->Vec<u8>{unsafe{std::slice::from_raw_parts(v as *const T as *const u8,size_of::<T>())}.to_vec()}

    #[test]
    fn buffer_offsets_of_one_alignment_class_share_a_bind_shape() {
        let buffer=|offset:u64|{let mut r=resource(1,2,0);r.length=65536;r.offset=offset;r};
        let shape=|offset:u64|compositor_plan_inputs_mode(&[buffer(offset)],true).unwrap().0.shape();
        assert_eq!(shape(0),shape(256));
        assert_eq!(shape(256),shape(4096+512));
        assert_eq!(shape(0),shape(4),"offsets are classed up to MAX_VERTEX_ALIGNMENT (4)");
        assert_ne!(shape(0),shape(2),"a finer offset alignment is another shape");
        let (state,_)=compositor_plan_inputs_mode(&[buffer(768)],true).unwrap();
        assert_eq!(state.position_of_shape([&shape(0)]),Some(0));
    }

    #[test]
    fn shape_plan_cache_returns_the_fresh_plan_bytes() {
        let (Some(air),Some(profile))=(render_fixture("probe.air"),compute_fixture("profile.json")) else {
            eprintln!("SKIP shape_plan_cache_returns_the_fresh_plan_bytes: fixtures unset");
            return;
        };
        use metal2vulkan::{metal::PixelFormat,pipeline::{ColorAttachmentDescriptor,RenderPipelineDescriptor,StageSource}};
        let library=Library::parse(&air).unwrap();
        let make=||{
            let description=DeviceDescription::from_khronos_json(std::str::from_utf8(&profile).unwrap(),None).unwrap();
            let device=Device::new(description,DevicePolicy::default()).unwrap();
            let mut descriptor=RenderPipelineDescriptor::new(StageSource::Function(library.descriptor("vt_vertex").unwrap()));
            descriptor.fragment=Some(StageSource::Function(library.descriptor("vt_fragment").unwrap()));
            descriptor.color_attachments[0]=Some(ColorAttachmentDescriptor::new(PixelFormat::BGRA8Unorm));
            let pipeline=device.compile_render_pipeline(&descriptor,&Budget::standard()).unwrap();
            Box::into_raw(Box::new(RenderPipelineHandle{_device:device,_pipeline:pipeline,source_blend:None,
                requested_attachments:vec![RenderColorAttachment{format:80,write_mask:15}],
                requested_attachment_blends:None,color_format:80,explicit_targets:false,plan_cache:Default::default(),descriptor:Default::default(),variants:Default::default()}))
        };
        let (warm,cold)=(make(),make());
        let draw=RenderDraw{version:1,vertex_start:0,vertex_count:3,instance_count:1,base_instance:0};
        let plan=|h:*mut RenderPipelineHandle,resources:&[RenderResource]|{
            let mut g=GenericPlan::default();let mut sp=GraphicsSpecializationPlan::default();let mut error=[0 as c_char;512];
            let rc=unsafe{tegu_air_render_pipeline_plan_generic_specialized(h,resources.as_ptr(),resources.len(),
                ptr::null(),0,&draw,&mut g,&mut sp,error.as_mut_ptr(),error.len())};
            assert_eq!(rc,0,"plan refused: {}",unsafe{std::ffi::CStr::from_ptr(error.as_ptr())}.to_string_lossy());
            (plan_bytes(&g),plan_bytes(&sp))
        };
        let a=[texture(1,0),linear_sampler(1,0),texture(2,0),linear_sampler(2,0)];
        let mut b=a;b[2].width=128;b[2].height=16; /* other facts, same shape class or not */
        let first=plan(warm,&a);let again=plan(warm,&a);
        assert_eq!(first,again);
        assert_eq!(unsafe{(*warm).plan_cache.lock().unwrap().len()},1,"equal shapes share one plan");
        let warm_b=plan(warm,&b);let cold_b=plan(cold,&b);
        assert_eq!(warm_b,cold_b,"a cached plan instantiates to the bytes of a fresh one");
        /* verify mode re-plans behind a hit and must agree */
        let (state,_)=compositor_plan_inputs_mode(&a,true).unwrap();
        let cached=unsafe{(*warm).cached_plan_mode(&state,true,true)}.unwrap();
        let fresh=unsafe{(*warm).cached_plan_mode(&state,false,false)}.unwrap();
        assert!(*cached==*fresh);
        unsafe{drop(Box::from_raw(warm));drop(Box::from_raw(cold));}
    }
}


#[cfg(test)]
mod generic_mrt_plan_tests {
    use super::*;

    #[test]
    fn bgr5a1_attachment_and_blend_are_admitted() {
        use metal2vulkan::{metal::PixelFormat,pipeline::ColorAttachmentDescriptor};
        assert_eq!(PixelFormat::try_from(43).unwrap(),PixelFormat::BGR5A1Unorm);
        let mut color=ColorAttachmentDescriptor::new(PixelFormat::BGR5A1Unorm);
        let blend=RenderBlend{version:1,enabled:1,rgb_op:0,rgb_src:1,rgb_dst:5,
            alpha_op:0,alpha_src:1,alpha_dst:5,write_mask:15};
        blend.apply(&mut color).unwrap();
        assert!(color.blending_enabled);
        assert_eq!(color.write_mask.as_raw(),15);
    }

    #[test]
    fn generic_target_sets_are_exact_and_mix_supported_formats() {
        const FORMATS:[u32;14]=[10,25,40,42,43,65,70,71,80,81,90,92,94,115];
        for raw in [1,11,30,31,12,13,14,20,22,23,24,32,33,34,53,54,55,60,62,63,64,72,73,74,91,103,104,105,110,112,113,114,123,124,125] {
            assert!(generic_target_formats([raw])&&generic_target_formats([80,raw]),"n2r target {raw} refused");
        }
        // Every ordered set of one to four targets from the five supported
        // formats is admitted.  This includes the c6ec [115,115] and [80,115]
        // blur targets, c6fc [43,43], and every mixed R8/BGR5A1/RGBA8/BGRA8/
        // RGBA16F combination.
        for len in 1..=4 {
            let total=FORMATS.len().pow(len as u32);
            for mut code in 0..total {
                let mut set=Vec::with_capacity(len);
                for _ in 0..len { set.push(FORMATS[code%FORMATS.len()]); code/=FORMATS.len(); }
                assert!(generic_target_formats(set.iter().copied()),
                    "admitted {len}-target set refused: {set:?}");
            }
        }
        assert!(generic_target_formats([43]));
        assert!(generic_target_formats([43,43]));
        assert!(generic_target_formats([80,43]));
        assert!(generic_target_formats([115,115]));
        assert!(generic_target_formats([80,115]));
        assert!(generic_target_formats([43,115,10,80]));
        // Exact refusals: zero targets, more than four, or any unsupported
        // format in any position (including Metal format0 gaps).
        assert!(!generic_target_formats([]));
        for len in 1..=4 {
            for pos in 0..len {
                // Formats n2r does not render (depth, compressed, packed
                // YUV, 9E5, XR) stay refused.
                for bad in [0,93,204,252,260,500,552] {
                    let mut set=vec![80;len];set[pos]=bad;
                    assert!(!generic_target_formats(set.iter().copied()),
                        "unsupported format {bad} admitted at len={len} pos={pos}");
                }
            }
        }
        assert!(!generic_target_formats([80,80,80,80,80]));
        assert!(!generic_target_formats([10,43,70,80,115,10]));
    }
}
