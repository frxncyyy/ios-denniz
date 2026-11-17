//! Translator format-admission audit: for every Metal pixel format and usage set, the
//! Vulkan representation `metal::format::vk_format` picks under the native profile JSON.
use std::collections::BTreeSet;
use metal2vulkan::device::{Device,DeviceDescription,DevicePolicy};
use metal2vulkan::device::FormatUsage as U;
use metal2vulkan::metal::{PixelFormat,format};
fn main(){
    let path=std::env::args().nth(1).expect("profile json");
    let json=std::fs::read_to_string(path).unwrap();
    let d=DeviceDescription::from_khronos_json(&json,None).unwrap();
    let dev=Device::new(d,DevicePolicy::default()).unwrap();
    let p=dev.profile();
    let sets:[(&str,&[U]);8]=[("sample",&[U::SampledImage]),("sample_linear",&[U::SampledImage,U::SampledImageFilterLinear]),
      ("render",&[U::ColorAttachment]),("render_blend",&[U::ColorAttachment,U::ColorAttachmentBlend]),
      ("storage",&[U::StorageImage]),("depth_stencil",&[U::DepthStencilAttachment]),
      ("texel_buffer",&[U::UniformTexelBuffer]),("storage_atomic",&[U::StorageImage,U::StorageImageAtomic])];
    for &pf in PixelFormat::ALL {
        let mut o=serde_json::Map::new();
        o.insert("raw".into(),pf.as_raw().into());
        o.insert("name".into(),format!("{pf:?}").into());
        o.insert("xr".into(),format!("{:?}",format::ExtendedRange::of(pf)).into());
        o.insert("ycbcr".into(),format::ycbcr(pf).into());
        o.insert("direct".into(),format!("{:?}",format::direct_format(pf)).into());
        for (name,us) in sets {
            let set:BTreeSet<U>=us.iter().copied().collect();
            let v=match format::vk_format(pf,&set,p){
                Ok(m)=>serde_json::json!({"ok":true,"vk":format!("{:?}",m.format),"emulation":format!("{:?}",m.emulation),
                    "swizzled":m.components!=metal2vulkan::vk::ComponentMapping::IDENTITY}),
                Err(e)=>serde_json::json!({"ok":false,"why":e.to_string()}),
            };
            o.insert(name.into(),v);
        }
        println!("{}",serde_json::Value::Object(o));
    }
}
