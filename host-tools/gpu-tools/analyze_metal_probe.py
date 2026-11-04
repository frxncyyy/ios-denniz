#!/usr/bin/env python3
"""Read the latest boot's complete native probe evidence, never qualify UI."""
from collections import Counter
from pathlib import Path
import argparse
import hashlib
import json
import re

BOOT = "=== NORD2-XNU LOADER: new run, previous log preserved ==="
PASSES = ("CLEAR+varying", "LOAD+scissor", "clear-only", "viewport", "blend+channel-mask")
VIEWPORT_PASSES = ("viewport-empty-clear", "viewport-negative", "viewport-scissor", "viewport-depth-reversed", "viewport-empty-load")
GEOMETRY = ("attribute-triangle", "attribute-quad", "generated-quad", "generated-quad-load")
FEATURES = ("tilefetch-batch", "preframe-partial", "batch-cap", "indexed-attr", "indexed-strip-restart",
            "async-many", "cmdpool-reuse")
TILER_VARIANTS = ("base", "chunk256k", "heap16m", "mesamask", "mask01",
                  "heap16m-256k-mesa", "constfs", "constfs-nosec",
                  "constfs-nosec-16m-256k-mesa", "spacer", "base-repeat")


def analyze(raw):
    text = raw.decode(errors="replace").split(BOOT)[-1]
    starts = list(re.finditer(r"NORD2_METAL probe start (batch-[\w-]+)", text))
    version = starts[-1][1] if starts else None
    text = text[starts[-1].end():] if starts else ""
    passes = [
        dict(format=int(f), pass_name=t, checked=int(n), mismatches=int(b))
        for f, t, n, b in re.findall(
            r"NORD2_METAL format=(\d+) pass=([\w+\-]+) checked=(\d+) mismatches=(\d+)", text)
    ]
    geometry = [
        dict(name=t, checked=int(n), mismatches=int(b))
        for t, n, b in re.findall(
            r"NORD2_METAL geometry=([\w-]+) checked=(\d+) mismatches=(\d+)", text)
    ]
    expected = Counter({(f, t): 2 if f == 80 else 1 for f in (80, 70, 10, 115) for t in (PASSES + VIEWPORT_PASSES if version in ("batch-viewport-v7", "batch-surface-v8", "batch-features-v9") else PASSES)})
    complete = re.findall(r"NORD2_METAL batch complete failed-targets=(\d+) iosurface-direct=(PASS|FAIL)", text)
    status = re.findall(r"NORD2_METAL status phase=batch-complete complete=(\d+) fault=(\d+) timeout=(\d+) live=(\d+) quarantine=(\d+)", text)
    cpu = re.findall(r"NORD2_METAL shared-texture format=(\d+) roundtrip=(PASS|FAIL)", text)
    compute = bool(re.search(r"NORD2_METAL compute checked=64 mismatches=0(?:\s|$)", text))
    expected_cpu = Counter({("80", "PASS"): 4, ("70", "PASS"): 2, ("10", "PASS"): 2, ("115", "PASS"): 2})
    if version in ("batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
        expected_cpu.update({("80", "PASS"): 2, ("70", "PASS"): 1, ("10", "PASS"): 1, ("115", "PASS"): 1})
    expected_geometry = Counter()
    if version in ("batch-geometry-v3", "batch-idvs-v4", "batch-tiler-v5", "batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
        expected_cpu[("80", "PASS")] += 4
        expected_geometry.update(GEOMETRY)
    qualified = bool(
        version in ("batch-pixels-v2", "batch-geometry-v3", "batch-idvs-v4", "batch-tiler-v5", "batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9") and compute
        and Counter((v["format"], v["pass_name"]) for v in passes) == expected
        and Counter(v["name"] for v in geometry) == expected_geometry
        and all(v["checked"] == 4096 and v["mismatches"] == 0 for v in passes + geometry)
        and Counter(cpu) == expected_cpu and complete == [("0", "PASS")]
        and len(status) == 1 and int(status[0][0]) > 0
        and all(int(status[0][i]) == 0 for i in (1, 2, 4))
        and "NORD2_METAL command failed" not in text
    )
    # batch-tiler-v5: one-shot tiler differential (bridge NORD2_METAL_TILER_MATRIX).
    matrix = {}
    for v, name, heap, chunk, mask, sec, fs, green, red, px in re.findall(
            r"NORD2_METAL tm v=(\d+) (\S+) heap=(\d+)K chunk=(\d) mask=([0-9a-f]+) sec=(\d) fs=(\w+) green=(\d+) red=(\d+) px=([0-9a-f]+)", text):
        matrix.setdefault(int(v), {}).update(name=name, heap_kib=int(heap), chunk_enum=int(chunk), mask=mask,
            secondary=sec == "1", fs=fs, green=int(green), red=int(red), center=px, rasterised=int(green) == 4096)
    for v, heapva, poly, bottom, blocks, first, last, top in re.findall(
            r"NORD2_METAL tm v=(\d+) heapva=([0-9a-f]+) poly=([0-9a-f]+) bottom=\+([0-9a-f]+) blocks=(\d+) first=([0-9a-f]+) last=([0-9a-f]+) top64k=(\d+)", text):
        matrix.setdefault(int(v), {}).update(heap_va=heapva, poly=poly, bottom_offset=bottom, nonzero_blocks=int(blocks),
            first_block=first, last_block=last, top64k_blocks=int(top))
    for v, words in re.findall(r"NORD2_METAL tm v=(\d+) state((?: [0-9a-f]{8}){16})", text):
        matrix.setdefault(int(v), {})["tiler_state"] = words.split()
    errors = re.findall(r"NORD2_METAL tm v=(\d+) (\S+) err=(-?\d+) broken=(\d)", text)
    ident = re.findall(r"NORD2_METAL tm id (.*)", text)
    adopt = re.findall(r"NORD2_METAL tm adopt=(-?\d+) heap=(\d+)K chunk=(\d) mask=([0-9a-f]+)", text)
    setup_errors = re.findall(r"NORD2_METAL tm setup err=(-?\d+)", text)
    skipped = re.findall(r"NORD2_METAL tm v=(\d+) (\S+) skipped: ([^\n]+)", text)
    # Missing/duplicated lines or failed cache maintenance must not qualify
    # a diagnostic build, even if later ordinary draws happen to pass.
    expected_variants = Counter(range(len(TILER_VARIANTS)))
    matrix_complete = bool(
        len(ident) == len(adopt) == 1 and not errors and not setup_errors and not skipped
        and all(Counter(map(int, re.findall(pattern, text))) == expected_variants
                for pattern in (r"NORD2_METAL tm v=(\d+) \S+ heap=",
                                r"NORD2_METAL tm v=(\d+) heapva=",
                                r"NORD2_METAL tm v=(\d+) state"))
        and all(matrix.get(i, {}).get("name") == name
                and len(matrix[i].get("tiler_state", [])) == 16
                and "heap_va" in matrix[i] and "green" in matrix[i]
                for i, name in enumerate(TILER_VARIANTS)))
    selected = matrix.get(int(adopt[0][0]), {}) if len(adopt) == 1 else {}
    adoption_verified = bool(matrix_complete and selected.get("rasterised")
        and selected.get("fs") == "tex" and selected.get("secondary")
        and selected.get("name") != "spacer"
        and selected.get("heap_kib") == int(adopt[0][1])
        and selected.get("chunk_enum") == int(adopt[0][2])
        and selected.get("mask") == adopt[0][3])
    tiler_matrix = dict(identity=ident[-1].strip() if ident else None,
                        variants=[dict(index=k, **matrix[k]) for k in sorted(matrix)],
                        errors=[dict(index=int(v), name=n, error=int(e), broken=b == "1") for v, n, e, b in errors],
                        complete=matrix_complete, adoption_verified=adoption_verified,
                        setup_errors=list(map(int, setup_errors)), skipped=skipped,
                        adopted=dict(index=int(adopt[-1][0]), heap_kib=int(adopt[-1][1]), chunk_enum=int(adopt[-1][2]),
                                     mask=adopt[-1][3]) if adopt else None) if version in ("batch-tiler-v5", "batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9") else None
    if version in ("batch-tiler-v5", "batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
        qualified = qualified and adoption_verified
    crashes = re.findall(r"CRASH nord2-metal-prob[^\n]*", text)
    policy = re.findall(r"NORD2_METAL memory-policy rc=(-?\d+) stage=(\d+) errno=(\d+) before=(-?\d+)/(-?\d+) requested=(\d+) active=(-?\d+)/(\d+) inactive=(-?\d+)/(\d+)", text)
    memory = [dict(phase=p, footprint=int(f), resident=int(r), peak=int(m))
              for p, f, r, m in re.findall(r"NORD2_METAL memory phase=([\w-]+) footprint=(\d+) resident=(\d+) peak=(\d+)", text)]
    memory_errors = re.findall(r"NORD2_METAL memory phase=[^\n]* error=[^\n]*", text)
    budget_verified = bool(len(policy) == 1 and policy[0][0:2] == ("0", "5")
        and policy[0][5:] == ("256", "256", "1", "256", "1")
        and all(sum(v['phase'] == p for v in memory) == 1
                for p in ('configured', 'compute-complete', 'batch-complete'))
        and all(0 < v['footprint'] <= 256 * 1024 * 1024 for v in memory)
        and not memory_errors)
    if version in ("batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
        qualified = qualified and budget_verified
    surface_reads = [dict(pass_name=p, checked=int(n), mismatches=int(b), lock=int(l),
                          unlock=int(u), layout=int(ok), stride=int(stride), center=center)
                     for p,n,b,l,u,ok,stride,center in re.findall(
        r"NORD2_METAL IOSurface pass=([\w+\-]+) checked=(\d+) mismatches=(\d+) lock=(-?\d+) unlock=(-?\d+) layout=(\d+) stride=(\d+) center=([0-9a-f]{8})", text)]
    surface_verified = bool(Counter(v['pass_name'] for v in surface_reads) == Counter(PASSES + VIEWPORT_PASSES)
        and all(v['checked'] == 4096 and v['mismatches'] == 0 and v['lock'] == v['unlock'] == 0
                and v['layout'] == 1 and v['stride'] >= 256 for v in surface_reads))
    if version in ("batch-surface-v8", "batch-features-v9"):
        qualified = qualified and surface_verified
    # batch-features-v9: tagged exact-pixel feature records, progress summaries
    # (emitted after every group, so a capture cut short still shows how far the
    # batch got) and the bridge's traced batch shapes/tiler-heap use.
    features = [dict(feature=t, checked=int(n), mismatches=int(b), result=r, detail=extra.strip())
                for t, n, b, r, extra in re.findall(
        r"NORD2_METAL feature=([\w-]+) checked=(\d+) mismatches=(\d+) result=(PASS|FAIL)([^\n]*)", text)]
    skipped_features = re.findall(r"NORD2_METAL feature=([\w-]+) result=SKIP reason=(\S+)", text)
    summaries = [dict(after=a, features=int(f), failed=int(x), elapsed_ms=int(e), log_used=int(u), log_dropped=int(d))
                 for a, f, x, e, u, d in re.findall(
        r"NORD2_METAL summary after=(\w+) features=(\d+) failed=(\d+) elapsed_ms=(\d+) log_used=(\d+) log_dropped=(\d+)", text)]
    batches = [dict(draws=int(n), preload=int(p), preframe=int(f), heap_used=int(u), heap=int(h))
               for n, p, f, u, h in re.findall(
        r"NORD2_METAL batch draws=(\d+) preload=(\d) preframe=(\d) heap-used=(\d+) heap=(\d+)", text)]
    complete_features = re.findall(r"NORD2_METAL batch complete [^\n]* features=(\d+) features-failed=(\d+)", text)
    per_feature = {t: [v for v in features if v["feature"] == t] for t in FEATURES}
    features_verified = bool(
        all(len(per_feature[t]) == 1 and per_feature[t][0]["result"] == "PASS"
            and per_feature[t][0]["checked"] > 0 and per_feature[t][0]["mismatches"] == 0 for t in FEATURES)
        and len(features) == len(FEATURES)
        and complete_features == [(str(len(FEATURES)), "0")])
    if version == "batch-features-v9":
        qualified = qualified and features_verified
        feature_report = dict(
            verified=features_verified,
            results={t: (per_feature[t][0]["result"] if len(per_feature[t]) == 1 else "MISSING" if not per_feature[t] else "DUPLICATE")
                     for t in FEATURES},
            records=features, skipped=[dict(feature=t, reason=r) for t, r in skipped_features],
            summaries=summaries, last_summary=summaries[-1] if summaries else None,
            batches=batches, max_batch_draws=max((b["draws"] for b in batches), default=0),
            max_heap_used=max((b["heap_used"] for b in batches), default=0))
    else:
        feature_report = None
    qualified = qualified and not crashes
    return dict(log_bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                batch_version=version, compute_verified=compute, passes=passes,
                geometry=geometry, cpu_roundtrips=cpu, batch_complete=complete,
                final_status=status, tiler_matrix=tiler_matrix, batch_hardware_qualified=qualified,
                crashes=crashes, memory_policy=policy, memory_samples=memory,
                memory_errors=memory_errors, memory_budget_verified=budget_verified,
                surface_reads=surface_reads, surface_reads_verified=surface_verified, features=feature_report,
                ui_qualified=False)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    result = json.dumps(analyze(args.log.read_bytes()), indent=2) + "\n"
    if args.output:
        args.output.write_text(result)
    print(result, end="")
