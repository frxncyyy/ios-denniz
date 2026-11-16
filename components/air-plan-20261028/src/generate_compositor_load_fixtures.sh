#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 3 ]]; then echo "usage: $0 MESA_SOURCE MESA_BUILD OUTPUT_DIR" >&2; exit 2; fi
mesa="$(realpath "$1")"
build="$(realpath "$2")"
out="$(realpath -m "$3")"
script_dir="$(cd "$(dirname "$0")" && pwd)"
packers="$script_dir/mesa/compositor_load_packers"
mkdir -p "$out"
common=(-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter
  -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0
  -I "$mesa/include" -I "$mesa/src/panfrost" -I "$mesa/src"
  -I "$mesa/src/panfrost/lib" -I "$build/src/panfrost/lib"
  -I "$build/src/panfrost" -I "$build/src" -I "$build/include")
cc "${common[@]}" "$packers/pack_aux.c" -lm -o "$out/pack_aux"
cc "${common[@]}" -I "$mesa/src/gallium/include" -I/usr/include/libdrm \
  "$packers/pack_preload_draw.c" "$build/src/panfrost/lib/libpan-format-v10.a" \
  -lm -o "$out/pack_draw"
python3 - "$build" "$packers/pack_texture.c" "$out" <<'PYCODE'
import json, os, shlex, subprocess, sys
build, source, out = sys.argv[1:]
rows=json.load(open(os.path.join(build,"compile_commands.json")))
row=next(x for x in rows if "pan_texture.c.o" in x.get("output","") and "arch-v10" in x.get("output",""))
args=shlex.split(row["command"]); clean=[]; skip=False
for arg in args:
    if skip: skip=False; continue
    if arg in ("-MD","-MMD"): continue
    if arg in ("-MF","-MQ"): skip=True; continue
    clean.append(arg)
clean[clean.index("-o")+1]=os.path.join(out,"pack_texture.o")
clean[-1]=os.path.abspath(source)
subprocess.run(clean,cwd=row["directory"],check=True)
base=os.path.join(build,"src/panfrost/lib")
util=os.path.join(build,"src/util/libmesa_util.a")
cmd=["cc",os.path.join(out,"pack_texture.o"),
 os.path.join(base,"libpan-arch-v10.a.p/pan_texture.c.o"),
 os.path.join(base,"libpan-arch-v10.a.p/pan_mod.c.o"),
 os.path.join(base,"libpanfrost_lib.a.p/pan_util.c.o"),
 os.path.join(base,"libpan-format-v10.a"),util,
 "-Wl,--gc-sections","-lm","-pthread","-o",os.path.join(out,"pack_texture")]
subprocess.run(cmd,check=True)
PYCODE
for spec in '16 16 64' '32 32 128' '256 256 1024' '1080 2424 4352'; do
  read -r w h stride <<<"$spec"; tag="${w}x${h}"
  "$out/pack_aux" "$out/$tag-fbd.bin" "$out/$tag-resource.bin" "$out/$tag-sync.bin" "$w" "$h"
  "$out/pack_draw" "$out/$tag-dcd.bin" "$out/$tag-rt.bin" "$w" "$h" "$stride" 0x05054000
  "$out/pack_texture" "$out/$tag-texture.bin" "$w" "$h" "$stride" 0x05054000
done
"$packers/build_load_shader_v10.sh" "$mesa" "$build" "$out/shader-build"
cp "$out/shader-build/generated-v10/load_shader_v10.bin" "$out/preload_shader_v10.bin"
python3 - "$out" "$mesa" "$packers" "$script_dir/generate_compositor_load_fixtures.sh" <<'PYCODE'
import hashlib,json,pathlib,subprocess,sys
out,mesa,packers,generator=map(pathlib.Path,sys.argv[1:])
commit=subprocess.check_output(["git","-C",str(mesa),"rev-parse","HEAD"],text=True).strip()
source_paths=["src/panfrost/lib/pan_fb_nir.c","src/panfrost/lib/pan_texture.c",
 "src/panfrost/lib/pan_texture.h","src/panfrost/lib/pan_mod.c",
 "src/panfrost/vulkan/panvk_vX_cmd_frame_shaders.c"]
sources={p:hashlib.sha256((mesa/p).read_bytes()).hexdigest() for p in source_paths}
for p in ["pack_aux.c","pack_preload_draw.c","pack_texture.c","compile_load_shader_v10.c","build_load_shader_v10.sh"]:
 sources["generator/"+p]=hashlib.sha256((packers/p).read_bytes()).hexdigest()
sources["generator/generate_compositor_load_fixtures.sh"]=hashlib.sha256(generator.read_bytes()).hexdigest()
generated={p.name:{"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
 for p in sorted(out.glob("*.bin"))}
(out/"provenance.json").write_text(json.dumps({"mesa_repository":"https://gitlab.freedesktop.org/mesa/mesa",
 "mesa_commit":commit,"architecture":"PAN_ARCH=10 (Valhall)",
 "target_format":"PIPE_FORMAT_B8G8R8A8_UNORM, linear, one mip/layer/sample",
 "texture_emitter":"GENX(pan_sampled_texture_emit) from src/panfrost/lib/pan_texture.c; exact compile_commands.json entry",
 "preload_shader":"pan_get_fb_shader_v10 built from included compile_load_shader_v10.c",
 "license":"Mesa sources are MIT licensed; see Mesa licenses/MIT",
 "source_sha256":sources,"generated":generated},indent=2)+"\\n")
PYCODE
echo "Generated Mesa BGRA LOAD fixtures under $out (host only)."
