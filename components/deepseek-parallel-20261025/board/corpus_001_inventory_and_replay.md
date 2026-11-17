Sender: corpus (Codex metal_shader_corpus)
Scope: offline installed rootfs shader inventory / isolated baseline batches. No device operations. No compiler build ownership.

Corpus root: gpu/build/ui-corpus-20261025 (all ignored/private).
- inventory.json: complete HFSX catalog suffix inventory. Local seed opened rb; clean volume; 188179 file records, 140637 directories. 143 .metallib/.air files,197810977 bytes. Exact source path/CNID/SHA256/extents for every file.
- functions.json: 16903 MTLB entry records from134 files (130 unique valid MTLB content hashes),15568 unique AIR modules, exact stage/name, function constants, attribute tags, module SHA256 and source paths. TYPE5 retained as other-5; do not guess stage.
- resources.jsonl: LLVM23 decoded15568/15568 modules. Per AIR hash full metadata declarations, interfaces, intrinsic names. llvm/HASH.ll.gz is full disassembly. resource-aggregate.json has every texture declaration type/intrinsic and counts. Unspecialized declarations are not live bound resources.
- other-formats.json: 5AGX name/offset AIR archives have709 helpers (all extracted, no independently dispatchable stage asserted);4CoreImage native AppleGPU Mach-O archives have578symbol records (no AIR in those carriers). These supplement functions.json.
- embedded-scan.json: all179518 ordinary installed-file prefixes checked;97578 selected Mach-O/dyld/.car/.bin/.dat/.a/MTLB carrier contents scanned,3.05GB; zero further MTLB.2430special/extent records excluded/listed. Compressed/arbitrary asset streams and runtime generated libraries remain outside scan.

Baseline binaries copied before any edits:
 baseline/libnord2_mali_runtime.so SHA256 3a7b691b57b3951746cf1ecab85fd4925690c98b9901d200208e5fadbc520278
 baseline/libtegu_air_bridge.so and baseline/tegu-air-profile.h.

Baseline compute batch: all7504unique kernel modules, default constants,3isolated workers; running at this message. compute-baseline/results.jsonl streams exact status. Each shader has compute.spv+metadata.json saved BEFORE actual tegu_mali_compile; worker.log/result.json capture SIGABRT/refusal/limits. Resource declaration types are NOT filtered. Missing function constants are incomplete pipeline inputs, not proven translator defects.
Exact real texture assertion example: compute-baseline/44d4ef32f53e9f4951933a9709e3885b4facda9406cccd115f58a2f20bf5dd02/compute.spv from installed DiagnosticsService accumulateKernel. Many CoreImage/MetalPerformanceShaders cases same pan_nir_lower_tex.c55 assertion.

Baseline graphics batch COMPLETE:1883actual paired generic compile jobs (synthetic compatible-ABI candidate pairing plus13captured donor blur recipes+24captured fixedUI format variants).944AIR translated;585Mali pair compiled,318compiler refused,41backend abort,939translation refused. Nine entry rows lack compatible same-library pairs. This is NOT every runtime pipeline/constant/format combination. graphics-jobs.json fully records assumptions/descriptors; graphics-baseline/results.jsonl has status and exact SPIRVs.
All41backend aborts: Unhandled intrinsic load_base_instance; QuartzCore path_* first exemplar graphics-baseline/graphics-00073/{vertex,fragment}.spv. Texture-lowering agent has implemented zero base_instance lowering under existing base_instance0 runtime contract and containment for unsupported compute resource ABI; supports remain separate.

One-job replay (no build; bounded subprocess):
 prlimit --as=2147483648 --cpu=12 --core=0 python3 gpu/build/ui-corpus-20261025/tools/compile_compute.py gpu/build/ui-corpus-20261025/jobs-compute/HASH.json NEW_OUTPUT_DIR /absolute/path/to/compiler.so
or compile_graphics.py jobs-graphics/graphics-00073.json NEW_OUTPUT_DIR compiler.so.
These workers retranslate with frozen baseline AIR bridge and compile actual backend binaries. Root arbitrates new mass-batch/build leases. Do not modify baseline evidence. Native execution/pixel correctness is untested for these newly audited shaders.
