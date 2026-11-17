use metal2vulkan_air::container::{Library, parse};
fn main() {
    for path in std::env::args().skip(1) {
        let bytes = std::fs::read(&path).unwrap();
        match parse(&bytes) {
            Ok(Library::Metallib(lib)) => {
                println!(
                    "{}: MTLB {:?}, {} functions",
                    path,
                    lib.header.version,
                    lib.functions.len()
                );
                for f in lib.functions {
                    let result = metal2vulkan_air::decode(
                        f.bitcode,
                        &mut metal2vulkan_air::budget::Meter::new(
                            &metal2vulkan_air::budget::Budget::standard(),
                        ),
                    );
                    println!(
                        "{} {:?} AIR {:?} {} bytes decode={}",
                        f.name,
                        f.stage,
                        f.air_version,
                        f.bitcode.len(),
                        match result {
                            Ok(_) => "OK".to_string(),
                            Err(e) => format!("REFUSED {:?}", e),
                        }
                    );
                }
            }
            Ok(Library::Module(m)) => println!("{}: single AIR {} bytes", path, m.stream.len()),
            Err(e) => println!("{}: REFUSED {:?}", path, e),
        }
    }
}
