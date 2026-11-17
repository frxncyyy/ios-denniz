#!/usr/bin/env python3
"""Extract QuartzCore/RenderBox -> Metal selectors from the local dyld cache.

Read-only.  The Apple cache and Metal framework headers stay in their existing
local locations; this script writes only selector names and provenance under
the integration worker directory.
"""
from __future__ import annotations

import hashlib
import json
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[4]
GPU = WORK / "gpu"
DSC = GPU / "build/frontend-audit-ui-c2-20261025/dsc/System/Library/Caches/com.apple.dyld"
DSC_TOOL = GPU / "build/ui-corpus-20261025/compositor-gate-20261025"
HEADERS = (GPU / "build/apple-toolchain/sdks/iPhoneOS16.5.sdk/System/Library/"
           "Frameworks/Metal.framework/Headers")
OUT_HEADER = HERE / "delta9_selectors.inc"
OUT_JSON = HERE / "delta9_selectors.json"

sys.path.insert(0, str(DSC_TOOL))
import dsc_qc  # noqa: E402

CACHE_IMAGES = {
    "QuartzCore": "/System/Library/Frameworks/QuartzCore.framework/QuartzCore",
    "RenderBox": "/System/Library/PrivateFrameworks/RenderBox.framework/RenderBox",
    "Metal": "/System/Library/Frameworks/Metal.framework/Metal",
}

CLASS_MAP = {
    "MTLDevice": ["TeguComputeDevice"],
    "MTLTexture": ["TeguTexture"],
    "MTLBuffer": ["TeguBuffer"],
    "MTLCommandQueue": ["TeguQueue"],
    "MTLCommandBuffer": ["TeguCommand"],
    "MTLCommandEncoder": ["TeguEncoder"],
    "MTLComputeCommandEncoder": ["TeguEncoder"],
    "MTLRenderCommandEncoder": ["TeguRenderEncoder"],
    "MTLBlitCommandEncoder": ["TeguBlitEncoder"],
    "MTLRenderPipelineState": ["TeguRenderPipeline"],
    "MTLComputePipelineState": ["TeguPipeline"],
    "MTLDepthStencilState": ["TeguDepthStencilState"],
    "MTLLibrary": ["TeguLibrary"],
    "MTLFunction": ["TeguFunction"],
    "MTLSamplerState": ["TeguSampler"],
    "MTLHeap": ["TeguHeap"],
    "MTLResource": ["TeguTexture", "TeguBuffer"],
    # C4 evidence: QuartzCore uses its own CA::Fence and does not send
    # MTLFence/MTLEvent/MTLSharedEvent messages.  Keep these visible but
    # classify them as proven-unreachable in this profile.
    "MTLFence": ["UNREACHABLE:CA::Fence"],
    "MTLEvent": ["UNREACHABLE:CA::Fence"],
    "MTLSharedEvent": ["UNREACHABLE:CA::Fence"],
}
UNREACHABLE_REASON = {
    "MTLFence": "C4 audit: CA uses CA::Fence; no MTLFence call path observed",
    "MTLEvent": "C4 audit: CA uses CA::Fence; no MTLEvent call path observed",
    "MTLSharedEvent": "C4 audit: CA uses CA::Fence; no MTLSharedEvent call path observed",
}


def _strip_pointer(value: int) -> int:
    # dyld shared-cache ObjC pointers carry a high tag/rebasing prefix.
    return value & 0xFFFFFFFFFF


def _u64(cache: dsc_qc.Cache, address: int) -> int:
    return struct.unpack("<Q", cache.read(address, 8))[0]


def _cstr(cache: dsc_qc.Cache, address: int, limit: int = 512) -> str:
    out = bytearray()
    for i in range(limit):
        char = cache.read(address + i, 1)
        if char == b"\0":
            break
        out.extend(char)
    return out.decode("utf-8", "replace")


def image_sections(cache: dsc_qc.Cache, path: str):
    base = cache.imgpath[path]
    return base, dsc_qc.macho_sections(cache, base)[1]


def selector_refs(cache: dsc_qc.Cache, path: str) -> set[str]:
    _base, sections = image_sections(cache, path)
    address, size = sections[("__DATA_CONST", "__objc_selrefs")]
    result = set()
    for offset in range(0, size, 8):
        target = _strip_pointer(_u64(cache, address + offset))
        try:
            value = _cstr(cache, target)
        except Exception:
            continue
        if value and len(value) <= 512:
            result.add(value)
    return result


def metal_header_selectors() -> dict[str, set[str]]:
    """Map protocol name -> selectors declared by that protocol (own methods)."""
    protocols: dict[str, set[str]] = {}
    for path in sorted(HEADERS.glob("*.h")):
        text = path.read_text(errors="ignore")
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        current: str | None = None
        body: list[str] = []
        for line in text.splitlines():
            match = re.match(r"\s*@protocol\s+(\w+)(?:\s*<[^>]*>)?\s*$", line)
            if match:
                current = match.group(1)
                body = []
                continue
            if current and re.match(r"\s*@end\b", line):
                protocols.setdefault(current, set()).update(
                    _protocol_selectors("\n".join(body))
                )
                current = None
                body = []
                continue
            if current is not None:
                body.append(line)
    return protocols


def _protocol_selectors(body: str) -> set[str]:
    selectors: set[str] = set()
    # Objective-C method declarations; the type expression may contain
    # parentheses (block parameters), so scan to the terminating semicolon.
    for match in re.finditer(r"[-+]\s*\((?:[^()]|\([^)]*\))*\)\s*([^;]+);",
                             body, re.S):
        declaration = " ".join(match.group(1).split())
        parts = re.findall(r"([A-Za-z_]\w*)\s*:", declaration)
        if parts:
            selectors.add("".join(part + ":" for part in parts))
        else:
            name = re.match(r"([A-Za-z_]\w*)", declaration)
            if name:
                selectors.add(name.group(1))
    # Properties are ordinary zero-argument getters unless a custom getter
    # is declared.  This includes inherited protocol declarations we parse.
    for match in re.finditer(r"@property\s*\(([^)]*)\)\s*([^;]+);", body, re.S):
        attrs, declaration = match.group(1), " ".join(match.group(2).split())
        getter = re.search(r"getter\s*=\s*([A-Za-z_]\w*)", attrs)
        if getter:
            selectors.add(getter.group(1))
            continue
        name = re.search(r"([A-Za-z_]\w*)\s*$", declaration)
        if name:
            selectors.add(name.group(1))
    return selectors


def main() -> int:
    cache = dsc_qc.Cache()
    refs = {name: selector_refs(cache, path) for name, path in CACHE_IMAGES.items()}
    header = metal_header_selectors()
    cases: dict[tuple[str, str, int, str], dict] = {}
    unclassified: set[str] = set()
    for image in ("QuartzCore", "RenderBox"):
        for selector in refs[image]:
            owners = [p for p, values in header.items() if selector in values]
            if not owners:
                unclassified.add(selector)
                continue
            for protocol in owners:
                targets = CLASS_MAP.get(protocol)
                if not targets:
                    unclassified.add(selector)
                    continue
                for target in targets:
                    if target.startswith("UNREACHABLE:"):
                        cases[(target.split(":", 1)[1], selector, 0,
                               protocol)] = {
                            "class": None, "selector": selector,
                            "protocol": protocol, "unreachable": True,
                            "reason": UNREACHABLE_REASON.get(protocol, target),
                        }
                    else:
                        cases[(target, selector, 0, protocol)] = {
                            "class": target, "selector": selector,
                            "protocol": protocol, "unreachable": False,
                        }
    ordered = sorted(cases.values(), key=lambda row: (
        row.get("class") or "", row["selector"], row["protocol"]))
    lines = ["/* Generated by delta9_extract_selectors.py from the local",
             " * QuartzCore/RenderBox dyld cache and Metal SDK headers.",
             " * Test-only selector names; no Apple binary is copied. */",
             "struct delta9_selector_case { const char *cls; const char *sel;",
             "                             int unreachable; const char *reason; };",
             "static const struct delta9_selector_case delta9_selectors[] = {"]
    for row in ordered:
        if row["unreachable"]:
            lines.append('    {0, "%s", 1, "%s"},' % (
                row["selector"].replace('"', '\\"'),
                row["reason"].replace('"', '\\"')))
        else:
            lines.append('    {"%s", "%s", 0, 0},' % (
                row["class"], row["selector"].replace('"', '\\"')))
    lines.append("};")
    lines.append("enum { delta9_selector_count = %d };" % len(ordered))
    OUT_HEADER.write_text("\n".join(lines) + "\n")
    meta = {
        "cache_files": {
            name: {
                "image": path,
                "selrefs": len(refs[name]),
            } for name, path in CACHE_IMAGES.items()
        },
        "dsc_root": str(DSC),
        "metal_header_dir": str(HEADERS),
        "metal_header_files": len(list(HEADERS.glob("*.h"))),
        "protocols": {name: len(values) for name, values in sorted(header.items())},
        "cases": len(ordered),
        "unclassified_selectors": sorted(unclassified),
        "unreachable": sum(1 for row in ordered if row["unreachable"]),
        "generated": str(OUT_HEADER),
        "generated_sha256": hashlib.sha256(OUT_HEADER.read_bytes()).hexdigest(),
    }
    OUT_JSON.write_text(json.dumps(meta, indent=2) + "\n")
    print(json.dumps({k: meta[k] for k in ("cases", "unreachable",
                                           "metal_header_files")}, indent=2))
    print("unclassified", len(unclassified))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
