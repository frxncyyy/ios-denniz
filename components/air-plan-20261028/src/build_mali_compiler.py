#!/usr/bin/env python3
"""Build pinned host Mali compiler libraries/tools for the native Metal bridge.

Requires an existing Mesa checkout at PIN and a Python venv with meson/mako/yaml.
No downloads, CI, phone operations, or claims of iOS compatibility. The upstream
panfrost_compile consumes OpenCL library SPIR-V; Vulkan AIR output still needs
our descriptor/system-value lowering adapter before the common Mali backend.
"""
import argparse
import os
import shutil
from pathlib import Path
import subprocess

PIN = '81daea66703eeea1a232446e3a36902353fc3045'
# This Mesa revision builds only nir_stub.c without a graphics/compute frontend.
# Select softpipe to retain compiler infrastructure (no softpipe target built).
# Without a Gallium driver Mesa resets with_any_opengl=false, even with opengl=true.
OPTIONS = [
    '-Dgallium-drivers=softpipe', '-Dvulkan-drivers=', '-Dtools=panfrost',
    '-Dplatforms=', '-Dglx=disabled', '-Degl=disabled', '-Dgbm=disabled',
    '-Dopengl=true', '-Dgles1=disabled', '-Dgles2=disabled', '-Dllvm=disabled',
    '-Dmesa-clc=auto', '-Dprecomp-compiler=auto', '-Dpanfrost-rust=false',
    '-Dbuild-tests=false',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--environment', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    source, build, environment = (p.resolve() for p in
                                  (args.source, args.build, args.environment))
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'],
                                     text=True).strip()
    if commit != PIN or args.jobs < 1:
        parser.error('require pinned Mesa revision and positive job count')
    # Add a separate host adapter target to the ignored checkout; no upstream
    # source files are patched beyond this delimited Meson target declaration.
    shutil.copyfile(Path(__file__).with_name('mali_compute_compiler.c'),
                    source/'src/panfrost/clc/tegu_mali_compute.c')
    shutil.copyfile(Path(__file__).with_name('pipeline_format.h'),
                    source/'src/panfrost/clc/pipeline_format.h')
    shutil.copyfile(Path(__file__).with_name('mali_compute_compiler.h'),
                    source/'src/panfrost/clc/mali_compute_compiler.h')
    config = source/'src/panfrost/clc/meson.build'
    marker = '# TEGU_HOST_ADAPTER_BEGIN'
    original = config.read_text().split(marker, 1)[0].rstrip()
    config.write_text(original + '\n\n' + marker + """
executable('tegu_mali_compute', ['tegu_mali_compute.c'],
  link_with : [libpanfrost_compiler],
  include_directories : [inc_include, inc_src],
  dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_model_dep],
  build_by_default : false)
shared_library('tegu_mali_compiler', ['tegu_mali_compute.c'],
  c_args : ['-DTEGU_MALI_LIBRARY_ONLY'],
  link_with : [libpanfrost_compiler],
  include_directories : [inc_include, inc_src],
  dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_model_dep],
  build_by_default : false)
# TEGU_HOST_ADAPTER_END
""")
    env = dict(os.environ, PATH=str(environment/'bin')+os.pathsep+os.environ['PATH'])
    meson = environment/'bin/meson'
    command = [str(meson), 'setup']
    if (build/'build.ninja').exists():
        command.append('--reconfigure')
    subprocess.run(command+[str(build), str(source)]+OPTIONS, env=env, check=True)
    subprocess.run(['ninja', '-C', str(build), '-j', str(args.jobs),
                    'src/panfrost/clc/panfrost_compile', 'src/panfrost/bifrost_compiler',
                    'src/panfrost/clc/tegu_mali_compute',
                    'src/panfrost/clc/libtegu_mali_compiler.so'],
                   env=env, check=True)


if __name__ == '__main__':
    main()
