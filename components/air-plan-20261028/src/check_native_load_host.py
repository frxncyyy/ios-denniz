#!/usr/bin/env python3
"""Run self-contained native LOAD C-state and GPU-alias host checks."""
import argparse
import hashlib
import json
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
FIXTURES = HERE / "fixtures"
AUTHORED = REPO / "tools/tegu/metal/fixtures"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, env):
    return subprocess.run(command, cwd=REPO, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          check=True).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-r7", type=Path,
                        help="optional external r7 packet for byte-parity provenance check")
    parser.add_argument("--work", type=Path,
                        help="optional directory for generated report; default is temporary")
    args = parser.parse_args()
    shared = HERE
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    report = {"scope": "host state derivation and MMU permission checks; no GPU execution"}
    with tempfile.TemporaryDirectory(prefix="tegu-load-") as temp:
        build = Path(temp)
        # Keep the authored regressions as their checked-in hex source; do not
        # add duplicate binary TGR copies to the proposal.
        legacy = build / "legacy-v1.tgr"
        sampled = build / "sampled-texture-v5.tgr"
        legacy.write_bytes(bytes.fromhex((AUTHORED / "legacy-v1.tgr.hex").read_text()))
        sampled.write_bytes(bytes.fromhex((AUTHORED / "sampled-texture-v5.tgr.hex").read_text()))
        state = build / "render-load-state"
        run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-I" + str(shared),
             str(shared / "render_load_host_test.c"),
             str(shared / "render_state.c"), str(shared / "vertex_fetch.c"),
             str(shared / "texture_state.c"), "-o", str(state)], env)
        raw = str(args.raw_r7.resolve()) if args.raw_r7 else ""
        state_out = run([str(state), str(FIXTURES / "framebuffer_load.bin"),
                         str(FIXTURES / "preload_dcd_page.bin"),
                         str(FIXTURES / "preload_resource_page.bin"),
                         str(FIXTURES / "preload_shader_v10.bin"),
                         str(legacy), raw, str(sampled)], env)
        mapping = build / "render-load-map"
        (build / "tegu_gpu_render_job.h").write_bytes((shared / "render_job.h").read_bytes())
        run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-I" + str(build), "-I" + str(HERE), "-I" + str(shared),
             str(HERE / "render_map_host_test.c"), "-o", str(mapping)], env)
        map_out = run([str(mapping)], env)
    report["state_builder"] = {"status": "PASS", "output": state_out.strip()}
    if args.raw_r7:
        report["optional_raw_r7"] = {"status": "PASS", "sha256": sha(args.raw_r7)}
    else:
        report["optional_raw_r7"] = "not supplied; no research artifact required"
    report["mapping"] = {"status": "PASS", "output": map_out.strip()}
    for name in ("framebuffer_load.bin", "preload_dcd_page.bin",
                 "preload_resource_page.bin", "preload_shader_v10.bin"):
        path = FIXTURES / name
        report.setdefault("fixture_sha256", {})[name] = sha(path)
    output = json.dumps(report, indent=2) + "\n"
    if args.work:
        args.work.mkdir(parents=True, exist_ok=True)
        (args.work / "native-load-host-result.json").write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
