#!/usr/bin/env python3
"""Build the experimental Metal compute plugin, without probe fixtures.

This packages the implementation for NSBundle loading. It does not register a
system device or assert full MTLDevice support. Use stubs from the donor OS.
"""
import argparse
import hashlib
import struct
import time
import json
from pathlib import Path
import plistlib
import shutil
import subprocess


# Metal framebuffer fetch arrives as Vulkan input attachments, which the
# translator admits only with rasterization-order color attachment access.
FRAMEBUFFER_FETCH_CAPABILITY = {
    'extensions': {'VK_EXT_rasterization_order_attachment_access': 1},
    'features': {'VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT':
                 {'rasterizationOrderColorAttachmentAccess': True}},
}


def framebuffer_fetch_profile(data):
    """Add the raster-order capability to every profile of a Khronos JSON."""
    document = json.loads(data)
    document.setdefault('capabilities', {})['m2v_raster_order_color'] = FRAMEBUFFER_FETCH_CAPABILITY
    for profile in document.get('profiles', {}).values():
        if 'm2v_raster_order_color' not in profile.setdefault('capabilities', []):
            profile['capabilities'].append('m2v_raster_order_color')
    return json.dumps(document, separators=(',', ':')).encode()


# Mali-G77 (arch9) samples ASTC LDR/sRGB 2D natively (n2t ASTC plane, Mesa
# pan_format ASTC_2D_LDR). The translator's feature ceiling is a desktop
# vulkaninfo profile without ASTC, so every pipeline sampling an ASTC texture
# (SpringBoard app snapshots: 1125x2436 ASTC_4x4_LDR) was refused with
# "needs Format VK_FORMAT_ASTC_4x4_UNORM_BLOCK SampledImage". The overlay adds
# the 2D block sizes the planner admits (air_bridge.rs generic_astc_2d_format:
# Metal 186-200 sRGB, 204-218 LDR, no HDR) as sampled + linear-filtered.
ASTC_BLOCKS = ('4x4', '5x4', '5x5', '6x5', '6x6', '8x5', '8x6', '8x8', '10x5', '10x6', '10x8', '10x10', '12x10', '12x12')
ASTC_FEATURES = ['VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT', 'VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT',
                 'VK_FORMAT_FEATURE_TRANSFER_SRC_BIT', 'VK_FORMAT_FEATURE_TRANSFER_DST_BIT']


def astc_ldr_profile(data):
    """Declare ASTC LDR/sRGB 2D sampling in every device capability of a Khronos JSON."""
    document = json.loads(data)
    for capability in document.get('capabilities', {}).values():
        features = capability.get('features', {}).get('VkPhysicalDeviceFeatures')
        if features is not None:
            features['textureCompressionASTC_LDR'] = True
        formats = capability.get('formats')
        if formats is None:
            continue
        for block in ASTC_BLOCKS:
            for kind in ('UNORM', 'SRGB'):
                formats['VK_FORMAT_ASTC_%s_%s_BLOCK' % (block, kind)] = {'VkFormatProperties': {
                    'linearTilingFeatures': list(ASTC_FEATURES), 'optimalTilingFeatures': list(ASTC_FEATURES),
                    'bufferFeatures': []}}
    return json.dumps(document, separators=(',', ':')).encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('out', 'bridge', 'mali', 'stubs', 'profile'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument("--protocols", action="store_true", help="Experimental donor protocol adoption and validated limits initialization")
    parser.add_argument('--device-create-diag', action='store_true',
                        help='Compile bounded device-construction phase markers (diagnostic bundle only)')
    parser.add_argument('--graphics', action='store_true', help='Experimental render pipeline compilation; requires rebuilt AIR/Mali graphics archives and kernel0122; initial16x16RGBAtriangle only')
    parser.add_argument('--compositor-profile', type=Path, help='Explicit translator profile for the qualified compositor class; keeps the legacy --profile unchanged')
    parser.add_argument('--attachment-clear-profile', type=Path, help='Exact translator profile for path_blit/attachment_clear TAC1 packaging')
    parser.add_argument('--render-trace', action='store_true', help='Opt-in first-16 pass and first-32 direct-draw metadata trace; no resource bytes or addresses')
    parser.add_argument('--fragment-bbox', action='store_true',
                        help='LOAD fragment passes limited to the union of draw scissors by default')
    parser.add_argument('--gpu-bench-default', metavar='OPTS',
                        help='run the GPU floor bench once per process start without the file flag (e.g. proc=backboardd)')
    parser.add_argument('--tile-fetch', action='store_true', help='Use qualified active-target tile fetch when available; requires the new Mali archive, TEGU_TILE_FETCH=0 disables')
    parser.add_argument('--tile-fetch-probe', type=Path, help='Generated authored tile-fetch assets header; run private-target GPU readback probe once in backboardd')
    parser.add_argument('--draw-reorder', action='store_true', help='Group independent generic draws within a render encoder; TEGU_DRAW_REORDER=0 disables')
    parser.add_argument('--small-heap', action='store_true', help='Opt-in measured heap bound for small nonindexed triangles; requires tile32 and prior heap calibration')
    parser.add_argument('--tile32', action='store_true', help='Use32x32 tiles and4KiB color allocation for generic BGRA8 jobs (TEGU_TILE32=0 disables)')
    parser.add_argument('--resource-share', action='store_true', help='Share byte-identical GPU resource tables within a draw batch (TEGU_BATCH_RESOURCES=0 disables)')
    parser.add_argument('--batch-merge', action='store_true',
                        help='Default-on batch merging: content-shared shader code (TEGU_BATCH_SHARE=0 off) and a 64 KiB additive tiler heap slack instead of scaling on small-job overruns (TEGU_BATCH_HEAP=0 off)')
    parser.add_argument('--astc-trace', action='store_true', help='First-16 ASTC texture create, upload and bind records only')
    parser.add_argument('--compiler-trace', action='store_true', help='Diagnostic first-16 compositor compiler phase, tick and image-address records')
    parser.add_argument('--pipeline-pack', type=Path, help='Pipeline pack from make_pipeline_pack.py for this exact bridge+Mali build (copied into the bundle)')
    parser.add_argument('--framebuffer-fetch', action='store_true', help='Add rasterization-order color attachment access to --profile so framebuffer-fetch shaders translate')
    args = parser.parse_args()
    if args.compositor_profile and not args.graphics:
        parser.error('compositor-profile requires graphics')
    if args.attachment_clear_profile and not args.graphics:
        parser.error('attachment-clear-profile requires graphics')
    if args.compiler_trace and not args.compositor_profile:
        parser.error('compiler-trace requires compositor-profile')
    source = Path(__file__).resolve().parent
    out, stubs = args.out.resolve(), args.stubs.resolve()
    names = ('Metal.tbd', 'Foundation.tbd', 'libobjc.A.dylib.tbd',
             'libSystem.B.dylib.tbd', 'libc++.1.dylib.tbd', 'libunwind.dylib.tbd')
    for name in names:
        if not (stubs / name).is_file():
            parser.error('Missing donor stub: ' + name)
    bundle = out / 'TeguMetal.bundle'
    bundle.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(stubs / 'libunwind.dylib.tbd', out / 'libunwind.tbd')
    profile = args.profile.read_bytes()
    if args.framebuffer_fetch:
        profile = framebuffer_fetch_profile(profile)
    (out / 'tegu-air-profile.h').write_text(
        'static const unsigned char tegu_native_profile[]={' +
        ','.join(map(str, profile)) + '};\n')
    compositor_profile=args.compositor_profile.read_bytes() if args.compositor_profile else None
    if compositor_profile is not None:
        (out / 'tegu-compositor-profile.h').write_text(
            'static const unsigned char tegu_compositor_profile[]={' +
            ','.join(map(str, compositor_profile)) + '};\n')
    attachment_clear_profile=args.attachment_clear_profile.read_bytes() if args.attachment_clear_profile else None
    if attachment_clear_profile is not None:
        (out / 'tegu-attachment-clear-profile.h').write_text(
            'static const unsigned char tegu_attachment_clear_profile[]={' +
            ','.join(map(str, attachment_clear_profile)) + '};\n')
    # Commit plus build time: identifies the executable a device actually loaded.
    head = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=source, capture_output=True, text=True).stdout.strip() or 'nogit'
    dirty = subprocess.run(['git', 'status', '--porcelain', '--untracked-files=no', '.'], cwd=source, capture_output=True, text=True).stdout.strip()
    bundle_id = head + ('-dirty' if dirty else '') + '-' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())
    # Pipeline cache entries are keyed by the compilers that produce them: a
    # different bridge or Mali archive invalidates every cached entry.
    cache_build = hashlib.sha256()
    for archive in (args.bridge, args.mali):
        cache_build.update(hashlib.sha256(archive.read_bytes()).digest())
    cache_build = cache_build.hexdigest()
    if args.tile_fetch_probe:
        (out / 'tegu-tile-fetch-assets.h').write_bytes(args.tile_fetch_probe.read_bytes())
    obj = out / 'compute_frontend.o'
    subprocess.run(['clang', '-fblocks', '--target=arm64-apple-ios16.0', '-ffreestanding',
                    '-O2', '-Wall', '-Wextra', '-Werror', '-fno-stack-protector',
                    '-fno-objc-arc', '-DTEGU_AIR_BRIDGE', '-DTEGU_MALI_BRIDGE',
                    '-DTEGU_METAL_BASE',
                    *(['-DTEGU_GRAPHICS_BRIDGE'] if args.graphics else []),
                    *([f'-DTEGU_GPU_BENCH_DEFAULT="{args.gpu_bench_default}"'] if args.gpu_bench_default else []),
                    *(['-DTEGU_FRAGMENT_BBOX_DEFAULT'] if args.fragment_bbox else []),
                    *(['-DTEGU_SMALL_HEAP_DEFAULT'] if args.small_heap else []),
                    *(['-DTEGU_TILE32_DEFAULT'] if args.tile32 else []),
                    *(['-DTEGU_RESOURCE_SHARE_DEFAULT'] if args.resource_share else []),
                    *(['-DTEGU_BATCH_MERGE_DEFAULT'] if args.batch_merge else []),
                    *(['-DTEGU_DRAW_REORDER_DEFAULT'] if args.draw_reorder else []),
                    *(['-DTEGU_TILE_FETCH_PROBE'] if args.tile_fetch_probe else []),
                    *(['-DTEGU_TILE_FETCH_DEFAULT'] if args.tile_fetch else []),
                    *(['-DTEGU_COMPOSITOR_PROFILE'] if compositor_profile is not None else []),
                    *(['-DTEGU_ATTACHMENT_CLEAR_PROFILE'] if attachment_clear_profile is not None else []),
                    *(['-DTEGU_METAL_FEATURE_INIT', '-DTEGU_METAL_PROTOCOLS'] if args.protocols else []),
                    *(['-DTEGU_DEVICE_CREATE_DIAG'] if args.device_create_diag else []),
                    *(['-DTEGU_RENDER_TRACE'] if args.render_trace else []),
                    *(['-DTEGU_ASTC_TRACE'] if args.astc_trace else []),
                    *(['-DTEGU_COMPILER_TRACE'] if args.compiler_trace else []),
                    '-DTEGU_BUNDLE_ID="' + bundle_id + '"',
                    '-DTEGU_PIPELINE_CACHE_BUILD="' + cache_build + '"',
                    '-I' + str(out), '-c',
                    str(source / 'compute_frontend.m'), '-o', str(obj)], check=True)
    executable = bundle / 'TeguMetal'
    subprocess.run(['ld64.lld', '-bundle', '-arch', 'arm64', '-platform_version',
                    'ios', '16.0', '16.0', '-adhoc_codesign', '-dead_strip',
                    '-L' + str(out), str(obj), str(args.bridge.resolve()),
                    str(args.mali.resolve()),
                    *[str(stubs / name) for name in names[:-1]],
                    '-o', str(executable)], check=True)
    info = {'CFBundleIdentifier': 'org.tegu.Metal', 'CFBundleExecutable': 'TeguMetal',
            'CFBundleName': 'TeguMetal', 'CFBundlePackageType': 'BNDL',
            'CFBundleVersion': '1', 'CFBundleShortVersionString': '0.1',
            'CFBundleSupportedPlatforms': ['iPhoneOS'], 'MinimumOSVersion': '16.0',
            'NSPrincipalClass': 'TeguComputeDevice'}
    pack_info = None
    if args.pipeline_pack:
        pack = args.pipeline_pack.read_bytes()
        tag = hashlib.sha256(struct.pack('<II', len(cache_build), 0) + cache_build.encode()).digest()[:8]
        if pack[:4] != b'TGPK' or pack[16:24] != tag:
            parser.error('pipeline pack was made for another bridge/Mali build')
        (bundle / 'PipelinePack.tpcpack').write_bytes(pack)
        pack_info = {'bytes': len(pack), 'entries': struct.unpack_from('<I', pack, 8)[0], 'sha256': hashlib.sha256(pack).hexdigest()}
    (bundle / 'Info.plist').write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    result = {'bundle': str(bundle), 'bundle_id': bundle_id, 'sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'profile_sha256': hashlib.sha256(profile).hexdigest(),
              'pipeline_cache_build': cache_build, 'pipeline_pack': pack_info,
              'contains_probe_fixtures': False, 'device_load_tested': False,
              'system_registered': False, 'full_metal_driver': False,
              'experimental_protocol_adoption': args.protocols,
              'batch_resource_sharing': args.resource_share,
              'generic_tile_pixels': 1024 if args.tile32 else 256,
              'small_draw_heap_bound': args.small_heap,
              'driver_limits_initializer': args.protocols,
              'device_create_diagnostics': args.device_create_diag,
              'bounded_render_trace': args.render_trace,
              'bounded_compiler_trace': args.compiler_trace,
              'render_pipeline_compilation': args.graphics, 'render_pipeline_import': args.graphics,
              'render_submission': args.graphics,
              'compositor_pipeline_compilation': compositor_profile is not None,
              'compositor_profile_sha256': hashlib.sha256(compositor_profile).hexdigest() if compositor_profile is not None else None,
              'attachment_clear_profile_sha256': hashlib.sha256(attachment_clear_profile).hexdigest() if attachment_clear_profile is not None else None,
              'compositor_load_submission': compositor_profile is not None,
              'compositor_iosurface_targets': compositor_profile is not None,
              'compositor_max_draws_per_command': 64 if compositor_profile is not None else None,
              'render_shape': ('Capability-gated BGRA compositor targets, CLEAR/LOAD and up to64 jobs per command'
                               if compositor_profile is not None else
                               '16x16 RGBA8 clear plus one nonindexed triangle/pass' if args.graphics else None)}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
