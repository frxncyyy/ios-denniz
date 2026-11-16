#!/usr/bin/env python3
"""Reject incomplete, mixed-boot, duplicate and failing device qualifications."""
from pathlib import Path
import importlib.util
import unittest

spec = importlib.util.spec_from_file_location("probe", Path(__file__).resolve().parents[1] / "tools/analyze_metal_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def fixture(version):
    lines = [f"NORD2_METAL probe start {version}", "NORD2_METAL compute checked=64 mismatches=0"]
    if version in ("batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
        lines += ["NORD2_METAL memory-policy rc=0 stage=5 errno=0 before=50/50 requested=256 active=256/1 inactive=256/1"]
        lines += [f"NORD2_METAL memory phase={p} footprint=72000000 resident=84000000 peak=80000000"
                  for p in ('configured', 'compute-complete', 'batch-complete')]
    if version in ("batch-geometry-v3", "batch-idvs-v4", "batch-tiler-v5", "batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
        for name in probe.GEOMETRY:
            lines += ["NORD2_METAL shared-texture format=80 roundtrip=PASS",
                      f"NORD2_METAL geometry={name} checked=4096 mismatches=0"]
    for f in (80, 70, 10, 115, 80):
        lines += [f"NORD2_METAL shared-texture format={f} roundtrip=PASS"] * (3 if version in ("batch-viewport-v7", "batch-surface-v8", "batch-features-v9") else 2)
        lines += [f"NORD2_METAL format={f} pass={p} checked=4096 mismatches=0" for p in (probe.PASSES + probe.VIEWPORT_PASSES if version in ("batch-viewport-v7", "batch-surface-v8", "batch-features-v9") else probe.PASSES)]
    if version in ("batch-tiler-v5", "batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
        lines += ["NORD2_METAL tm id gpu=90800011 tiler=00000809"]
        for v, name in enumerate(probe.TILER_VARIANTS):
            lines += [
                f"NORD2_METAL tm v={v} {name} heap=2048K chunk=0 mask=082 sec=1 fs=tex green=4096 red=0 px=ff00ff00",
                f"NORD2_METAL tm v={v} heapva=1000200000 poly=820010003fff80 bottom=+10000 blocks=1 first=0 last=0 top64k=0 vtx=0/0",
                f"NORD2_METAL tm v={v} state" + " 00000000" * 16]
        lines += ["NORD2_METAL tm adopt=1 heap=2048K chunk=0 mask=082"]
    if version in ("batch-surface-v8", "batch-features-v9"):
        lines += [f"NORD2_METAL IOSurface pass={p} checked=4096 mismatches=0 lock=0 unlock=0 layout=1 stride=256 center=ff4080bf"
                  for p in probe.PASSES + probe.VIEWPORT_PASSES]
    tail = ""
    if version == "batch-features-v9":
        lines += ["NORD2 GPU USER: NORD2_METAL batch draws=100 preload=0 preframe=1 heap-used=67584 heap=2097152"]
        lines += [f"NORD2_METAL feature={t} checked=4096 mismatches=0 result=PASS" for t in probe.FEATURES]
        lines += ["NORD2_METAL feature=async-fault result=SKIP reason=context-unsafe",
                  "NORD2_METAL summary after=features features=7 failed=0 elapsed_ms=4000 log_used=9000 log_dropped=120"]
        tail = " features=7 features-failed=0"
    lines += ["NORD2_METAL batch complete failed-targets=0 iosurface-direct=PASS" + tail,
              "NORD2_METAL status phase=batch-complete complete=50 fault=0 timeout=0 live=4 quarantine=0"]
    return "\n".join(lines).encode()


class Qualification(unittest.TestCase):
    def test_complete(self):
        for version in ("batch-pixels-v2", "batch-geometry-v3", "batch-idvs-v4", "batch-tiler-v5", "batch-tiler-v6", "batch-viewport-v7", "batch-surface-v8", "batch-features-v9"):
            result = probe.analyze(fixture(version))
            self.assertTrue(result["batch_hardware_qualified"])
            self.assertFalse(result["ui_qualified"])

    def test_failures(self):
        good = fixture("batch-geometry-v3")
        variants = [
            good.rsplit(b"\n", 1)[0], good.replace(b"mismatches=0", b"mismatches=1", 1),
            good.replace(b"fault=0", b"fault=1"), good.replace(b"quarantine=0", b"quarantine=1"),
            good.replace(b"geometry=generated-quad ", b"geometry=attribute-quad "),
            good.replace(b"roundtrip=PASS", b"roundtrip=FAIL", 1),
            good.replace(b"format=115", b"format=70"),
            good.replace(b"batch-geometry-v3", b"batch-unrecognized"),
            good + b"\nNORD2_METAL command failed",
            good + b"\nCRASH nord2-metal-prob[70] sig 9 code 0x0 sub 0x0",
            good + b"\n" + probe.BOOT.encode() + b"\nboot still starting",
            good + b"\nNORD2_METAL probe start batch-geometry-v3\n",
        ]
        for i, bad in enumerate(variants):
            with self.subTest(case=i):
                self.assertFalse(probe.analyze(bad)["batch_hardware_qualified"])

    def test_matrix_requires_complete_coherent_evidence(self):
        good = fixture("batch-tiler-v5")
        lines = good.splitlines()
        # Losing any part of the matrix cannot be hidden by a passing batch.
        for line in lines:
            if b"NORD2_METAL tm " in line:
                for bad in (b"\n".join(x for x in lines if x != line),
                            good + b"\n" + line):
                    self.assertFalse(probe.analyze(bad)["batch_hardware_qualified"])
        for suffix in (b"NORD2_METAL tm setup err=-10",
                       b"NORD2_METAL tm v=0 base err=-16 broken=1",
                       b"NORD2_METAL tm v=3 mesamask skipped: no TILER_FEATURES levels"):
            self.assertFalse(probe.analyze(good + b"\n" + suffix)["batch_hardware_qualified"])
        for old, new in ((b"adopt=1 ", b"adopt=-1 "),
                         (b"adopt=1 ", b"adopt=9 "),
                         (b"adopt=1 heap=2048K", b"adopt=1 heap=16384K"),
                         (b"green=4096", b"green=0"),
                         (b"state 00000000", b"state INVALID")):
            self.assertFalse(probe.analyze(good.replace(old, new))["batch_hardware_qualified"])

    def test_viewport_requires_every_case(self):
        good = fixture("batch-viewport-v7")
        result = probe.analyze(good)
        self.assertEqual(len(result["passes"]), 50)
        for line in good.splitlines():
            if b"pass=viewport" in line:
                for bad in (b"\n".join(x for x in good.splitlines() if x != line),
                            good + b"\n" + line,
                            good.replace(line, line.replace(b"mismatches=0", b"mismatches=752"))):
                    self.assertFalse(probe.analyze(bad)["batch_hardware_qualified"])
        self.assertFalse(probe.analyze(good.replace(b"inactive=256/1", b"inactive=6/1"))["batch_hardware_qualified"])

    def test_surface_requires_all_direct_reads_and_successful_locks(self):
        good = fixture("batch-surface-v8")
        self.assertTrue(probe.analyze(good)["surface_reads_verified"])
        for line in good.splitlines():
            if b"NORD2_METAL IOSurface pass=" in line:
                for bad in (b"\n".join(x for x in good.splitlines() if x != line), good + b"\n" + line):
                    self.assertFalse(probe.analyze(bad)["batch_hardware_qualified"])
                for old,new in ((b"checked=4096",b"checked=0"), (b"mismatches=0",b"mismatches=1"),
                                (b"lock=0",b"lock=-1"), (b"unlock=0",b"unlock=-1"),
                                (b"layout=1",b"layout=0"), (b"stride=256",b"stride=128")):
                    self.assertFalse(probe.analyze(good.replace(line,line.replace(old,new)))["batch_hardware_qualified"])
        self.assertFalse(probe.analyze(good.replace(b"iosurface-direct=PASS", b"iosurface-direct=FAIL"))["batch_hardware_qualified"])

    def test_memory_policy_requires_verified_budget_and_samples(self):
        good = fixture("batch-tiler-v6")
        for line in good.splitlines():
            if b"NORD2_METAL memory" in line:
                for bad in (b"\n".join(x for x in good.splitlines() if x != line), good + b"\n" + line):
                    self.assertFalse(probe.analyze(bad)["batch_hardware_qualified"])
        for old, new in ((b"rc=0", b"rc=-1"), (b"stage=5", b"stage=3"),
                         (b"active=256/1", b"active=50/1"),
                         (b"inactive=256/1", b"inactive=256/0"),
                         (b"footprint=72000000", b"footprint=999999999")):
            self.assertFalse(probe.analyze(good.replace(old, new))["batch_hardware_qualified"])
        for suffix in (b"CRASH nord2-metal-prob[70] sig 9",
                       b"NORD2_METAL memory phase=render-complete error=1 count=0"):
            self.assertFalse(probe.analyze(good + b"\n" + suffix)["batch_hardware_qualified"])


class Features(unittest.TestCase):
    def test_v9_requires_every_feature_once_and_passing(self):
        good = fixture("batch-features-v9")
        r = probe.analyze(good)
        self.assertTrue(r["batch_hardware_qualified"])
        self.assertTrue(r["features"]["verified"])
        self.assertEqual(r["features"]["max_batch_draws"], 100)
        self.assertEqual(r["features"]["last_summary"]["after"], "features")
        self.assertEqual(r["features"]["skipped"], [dict(feature="async-fault", reason="context-unsafe")])
        for line in good.splitlines():
            if b"NORD2_METAL feature=" in line and b"SKIP" not in line:
                for bad in (b"\n".join(x for x in good.splitlines() if x != line), good + b"\n" + line,
                            good.replace(line, line.replace(b"mismatches=0", b"mismatches=3")),
                            good.replace(line, line.replace(b"result=PASS", b"result=FAIL")),
                            good.replace(line, line.replace(b"checked=4096", b"checked=0"))):
                    self.assertFalse(probe.analyze(bad)["batch_hardware_qualified"])
        for old, new in ((b"features-failed=0", b"features-failed=1"), (b"features=7 features-failed", b"features=6 features-failed")):
            self.assertFalse(probe.analyze(good.replace(old, new))["batch_hardware_qualified"])
        # A capture that ends early still reports how far the batch got.
        cut = good.split(b"NORD2_METAL batch complete")[0]
        r = probe.analyze(cut)
        self.assertFalse(r["batch_hardware_qualified"])
        self.assertEqual(r["features"]["results"]["batch-cap"], "PASS")
        self.assertEqual(r["features"]["last_summary"]["features"], 7)


if __name__ == "__main__":
    unittest.main()
