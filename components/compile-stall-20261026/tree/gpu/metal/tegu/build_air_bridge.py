#!/usr/bin/env python3
"""Build the native-callable AIR bridge against the pinned translator checkout."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

PIN = '720ef1ce9ae2a2068999a8675717418b9416283e'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--translator', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--target', help='Rust target triple; requires its installed std')
p.add_argument('--static-only', action='store_true', help='Do not link a shared library (cross build)')
p.add_argument('--rust-sysroot', type=Path, help='Private target Rust sysroot; leaves system installation untouched')
a = p.parse_args()
source = a.translator.resolve()
if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip() != PIN:
    p.error('translator commit differs from audited pin')
work = a.work.resolve()
repo = Path(__file__).resolve().parents[3]
if not work.is_relative_to(repo/'research'):
    p.error('build directory must be beneath research/')
(work/'src').mkdir(parents=True,exist_ok=True)
shutil.copyfile(Path(__file__).with_name('air_bridge.rs'),work/'src/lib.rs')
shutil.copyfile(Path(__file__).with_name('compositor_contract.json'),work/'src/compositor_contract.json')
shutil.copyfile(Path(__file__).with_name('compositor_contract_v7.json'),work/'src/compositor_contract_v7.json')
shutil.copyfile(Path(__file__).with_name('home_affordance_contract.json'),work/'src/home_affordance_contract.json')
shutil.copyfile(Path(__file__).with_name('narrow_blur_11_contract.json'),work/'src/narrow_blur_11_contract.json')
shutil.copyfile(Path(__file__).with_name('narrow_blur_7_contract.json'),work/'src/narrow_blur_7_contract.json')
shutil.copyfile(Path(__file__).with_name('single_pass_blur_3_contract.json'),work/'src/single_pass_blur_3_contract.json')
shutil.copyfile(Path(__file__).with_name('quartzcore_pipeline_catalog.json'),work/'src/quartzcore_pipeline_catalog.json')
(work/'Cargo.toml').write_text(
    '[package]\nname="tegu-air-bridge"\nversion="0.1.0"\nedition="2024"\n'
    '[lib]\ncrate-type='+json.dumps(['staticlib'] if a.static_only else ['staticlib','cdylib'])+'\n'
    '[dependencies]\nmetal2vulkan={path='+json.dumps(str(source/'crates/metal2vulkan'))+',features=["serde"]}\n'
    'serde_json="1"\n[profile.release]\ndebug=1\n'
    # Fat LTO, one codegen unit: smaller code to page in and ~3 % less AIR time per pipeline
    # (compiler-speed host measurement); panic stays unwind (air_bridge.rs uses catch_unwind).
    'lto="fat"\ncodegen-units=1\n')
cmd=['cargo','build','--release','--manifest-path',str(work/'Cargo.toml')]
if a.target: cmd+=['--target',a.target]
env=dict(os.environ)
if a.rust_sysroot:
    if not a.target:
        p.error('--rust-sysroot requires --target')
    env['CARGO_ENCODED_RUSTFLAGS']='\x1f'.join(['--sysroot',str(a.rust_sysroot.resolve())])
subprocess.run(cmd,check=True,env=env)
