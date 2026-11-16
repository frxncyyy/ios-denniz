#!/usr/bin/env python3
"""Generate compile-time facts from the executed gradient-r3 proof artifacts."""
import hashlib
import json
import struct
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proof-directory", type=Path, required=True, help="Directory containing candidate.bin, candidate.json and expected-rgba8.bin")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    PROOF = args.proof_directory.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    candidate = (PROOF / "candidate.bin").read_bytes()
    metadata = json.loads((PROOF / "candidate.json").read_text())
    rgba = (PROOF / "expected-rgba8.bin").read_bytes()
    if metadata.get("texture_gradient") != 1 or metadata.get("gpu_executed") is not False:
        raise SystemExit("expected captured-pair gradient proof candidate metadata")
    if hashlib.sha256(rgba).hexdigest() != metadata.get("expected_rgba8_sha256"):
        raise SystemExit("gradient proof output oracle hash mismatch")
    facts = {
        "vertex_vs2": candidate[0x57300:0x57340],
        "vertex_vs3": candidate[0x57340:0x57350],
        "fragment_fs1": candidate[0x57350:0x573a0],
        "fragment_fs6": candidate[0x573a0:0x573b0],
        "source_rgba8": candidate[0x58000:0x58400],
        "captured_vertices": candidate[0x55000:0x55060],
        "expected_rgba8": rgba,
    }
    if [len(facts[k]) for k in facts] != [64, 16, 80, 16, 1024, 96, 1024]:
        raise SystemExit("raw proof facts have unexpected extent")
    if hashlib.sha256(facts["source_rgba8"]).hexdigest() != metadata.get("gradient_source_sha256"):
        raise SystemExit("captured gradient texture hash mismatch")
    if facts["vertex_vs2"] != struct.pack("<16f", 1,0,0,0, 0,1,0,0,
                                                    0,0,1,0, 0,0,0,1):
        raise SystemExit("raw proof VS2 transform mismatch")
    if facts["vertex_vs3"] != struct.pack("<4f", 1,1,0,0):
        raise SystemExit("raw proof VS3 transform mismatch")
    if facts["fragment_fs1"][:16] != bytes(16) or struct.unpack_from("<4H", facts["fragment_fs1"], 64) != (0x3400,) * 4:
        raise SystemExit("raw proof FS1 offsets/weights mismatch")
    if struct.unpack_from("<H", facts["fragment_fs6"])[0] != 0x3c00:
        raise SystemExit("raw proof FS6 opacity mismatch")
    source_formula = bytes(c for y in range(16) for x in range(16)
                           for c in (11 + 13*x, 19 + 11*y, 7 + 3*x + 5*y, 255))
    if source_formula != facts["source_rgba8"]:
        raise SystemExit("captured source is not the gradient-oracle texture")
    # Transform the executed raw RGBA8 oracle into BGRA8 target memory order.
    bgra = bytearray(rgba)
    for p in range(0, len(bgra), 4):
        bgra[p], bgra[p + 2] = bgra[p + 2], bgra[p]
    facts["expected_bgra8"] = bytes(bgra)
    # Repack the exact executed Float4/Float2 vertices into the requested
    # public descriptor's 40-byte stride. Keep all six reflected components.
    records = []
    for i in range(3):
        v = struct.unpack_from("<4f2f2I", facts["captured_vertices"], i * 32)
        records.append(struct.pack("<4f2f2I2I", *v, 0, 0))
    facts["vertices_stride40"] = b"".join(records)
    if len(facts["vertices_stride40"]) != 120:
        raise SystemExit("stride-40 vertex pack failed")
    # Derive clear RGBA values from the raw proof's packed clear word.
    clear = int(metadata["target_clear"])
    clear_rgba = (clear & 255, (clear >> 8) & 255, (clear >> 16) & 255, (clear >> 24) & 255)
    if clear_rgba != (0xcc, 0x66, 0x33, 0xff):
        raise SystemExit("unexpected executed proof clear color")
    words = struct.unpack("<256I", rgba)
    if sum(w != int(metadata["target_clear"]) for w in words) != 66:
        raise SystemExit("executed output oracle coverage changed")
    lines = ["/* Generated from the executed compositor gradient proof; do not edit. */"]
    for name, blob in facts.items():
        lines.append(f"static const unsigned char tegu_{name}[{len(blob)}] = {{")
        for start in range(0, len(blob), 16):
            lines.append("  " + ",".join(f"0x{x:02x}" for x in blob[start:start + 16]) + ",")
        lines.append("};")
    lines += [
        f"#define TEGU_PROOF_CLEAR_R {clear_rgba[0]}",
        f"#define TEGU_PROOF_CLEAR_G {clear_rgba[1]}",
        f"#define TEGU_PROOF_CLEAR_B {clear_rgba[2]}",
        f"#define TEGU_PROOF_CLEAR_A {clear_rgba[3]}",
        "",
    ]
    (out / "compositor_proof_facts.h").write_text("\n".join(lines))
    provenance = {
        "proof_candidate": str(PROOF / "candidate.bin"),
        "candidate_sha256": hashlib.sha256(candidate).hexdigest(),
        "proof_metadata": str(PROOF / "candidate.json"),
        "proof_metadata_sha256": hashlib.sha256((PROOF / "candidate.json").read_bytes()).hexdigest(),
        "expected_rgba8": str(PROOF / "expected-rgba8.bin"),
        "expected_rgba8_sha256": hashlib.sha256(rgba).hexdigest(),
        "expected_bgra8_sha256": hashlib.sha256(bgra).hexdigest(),
        "source_rgba8_sha256": hashlib.sha256(facts["source_rgba8"]).hexdigest(),
        "ubo_sha256": {k: hashlib.sha256(facts[k]).hexdigest() for k in
                       ("vertex_vs2", "vertex_vs3", "fragment_fs1", "fragment_fs6")},
        "vertex_stride40_sha256": hashlib.sha256(facts["vertices_stride40"]).hexdigest(),
        "raw_proof_execution": str(PROOF.parents[2] / "device-result-r3.json"),
        "raw_proof_execution_sha256": hashlib.sha256((PROOF.parents[2] / "device-result-r3.json").read_bytes()).hexdigest(),
        "clear_rgba": clear_rgba,
        "limits": "This builds a phone probe but does not run it; the raw proof executes Mali code in the isolated raw queue and is not native Metal execution.",
    }
    (out / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(json.dumps(provenance, indent=2))


if __name__ == "__main__":
    main()
