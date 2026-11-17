#!/usr/bin/env python3
"""Independent tests for gpu/tools/audit_shader_pipeline_inputs.py.

These tests are deliberately *not* a replay of the tool's ``selftest``: the expected
bytes, descriptor arrays and outcome classes are written out independently, and the
crash/resume accounting and ledger I/O are exercised with temporary fixtures.

Run:
    python3 -m unittest -v test_audit_shader_pipeline_inputs.py
"""
from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve()
ROOT = HERE.parents[5]
TOOL = ROOT / "gpu" / "tools" / "audit_shader_pipeline_inputs.py"
SPEC = importlib.util.spec_from_file_location("audit_inputs", TOOL)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class TypeEncodingTests(unittest.TestCase):
    def test_published_data_type_values(self):
        self.assertEqual(audit.mtl_type("bool").numeric, 53)
        self.assertEqual(audit.mtl_type("ushort").numeric, 41)
        self.assertEqual(audit.mtl_type("uint").numeric, 33)
        self.assertEqual(audit.mtl_type("half").numeric, 16)
        self.assertEqual(audit.mtl_type("half4").numeric, 19)
        self.assertEqual(audit.mtl_type("uchar3").numeric, 51)

    def test_exact_byte_widths(self):
        expected = {
            "bool": 1, "uchar": 1, "short": 2, "ushort": 2, "int": 4,
            "uint": 4, "half": 2, "float": 4, "uchar3": 3, "half3": 6,
            "float3": 12, "uint2": 8, "int4": 16,
        }
        for name, size in expected.items():
            self.assertEqual(audit.mtl_type(name).size, size, name)

    def test_little_endian_values(self):
        self.assertEqual(audit.encode_scalar("bool", True), b"\x01")
        self.assertEqual(audit.encode_scalar("ushort", 0x1234), b"\x34\x12")
        self.assertEqual(audit.encode_scalar("uint", 0x89ABCDEF), b"\xef\xcd\xab\x89")
        self.assertEqual(audit.encode_scalar("int", -2), b"\xfe\xff\xff\xff")
        self.assertEqual(audit.encode_scalar("half", 0.5), b"\x00\x38")
        self.assertEqual(audit.encode_scalar("half", 1.0), b"\x00\x3c")
        self.assertEqual(audit.encode_scalar("uchar3", [1, 2, 3]), b"\x01\x02\x03")
        self.assertIsNone(audit.encode_scalar("uchar", 256))


class MetadataTests(unittest.TestCase):
    LL = r'''
!air.function_constants = !{!26, !27}
!26 = !{ptr addrspace(2) @_ZN5V3HLR14kXhlrbTileSizeE.MTL_FC_INIT_0_t, !"ushort", !"V3HLR::kXhlrbTileSize", i32 0, i1 true}
!27 = !{ptr addrspace(2) @_Z16fc_vertex_layout.MTL_FC_INIT_3_h, !"uchar", !"fc_vertex_layout", i32 3, i1 false}
!10 = !{i32 0, !"air.vertex_input", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"float4", !"air.arg_name", !"pos"}
!11 = !{i32 1, !"air.vertex_input", !"air.location_index", i32 1, i32 1, !"air.arg_type_name", !"half2", !"air.arg_name", !"uv"}
!12 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4"}
!13 = !{i32 2, !"air.render_target", i32 3, !"air.arg_type_name", !"uint"}
!14 = !{i32 0, !"air.patch_control_point_input", !15, !16}
!15 = !{!"air.patch_control_point_function", ptr @_Z12ControlPoint.MTL_CONTROL_POINT_FN}
!16 = !{!"air.location_index", i32 2, i32 1, !"air.arg_type_name", !"float"}
'''

    def test_constants_from_air_not_container(self):
        rows = audit.parse_function_constants_ll(self.LL)
        self.assertEqual([r["index"] for r in rows], [0, 3])
        self.assertEqual(rows[0]["type"], "ushort")
        self.assertTrue(rows[0]["required"])
        self.assertEqual(rows[1]["type"], "uchar")
        self.assertFalse(rows[1]["required"])

    def test_ordinary_vertex_inputs(self):
        rows = audit.parse_vertex_inputs_ll(self.LL)
        self.assertEqual(rows, [
            {"index": 0, "count": 1, "type": "float4"},
            {"index": 1, "count": 1, "type": "half2"},
        ])

    def test_render_targets_preserve_input_and_output(self):
        rows = audit.parse_render_targets_ll(self.LL)
        self.assertEqual(
            {(r["slot"], r["form"], r["type"]) for r in rows},
            {(0, "output", "float4"), (3, "input", "uint")},
        )

    def test_patch_inputs_follow_group_references(self):
        rows = audit.parse_patch_inputs_ll(self.LL)
        self.assertEqual(rows, [{"index": 2, "type": "float"}])

    def test_patch_marker_is_not_control_point_metadata(self):
        self.assertFalse(audit.patch_shader(self.LL))
        self.assertTrue(audit.patch_shader('!1 = !{!"air.patch", !"quad"}'))


class AttachmentPlanningTests(unittest.TestCase):
    def test_four_float_targets_use_bgra_and_r8(self):
        plan = audit.plan_attachments([
            {"slot": 0, "form": "output", "type": "float4"},
            {"slot": 1, "form": "output", "type": "float4"},
            {"slot": 2, "form": "input", "type": "float"},
            {"slot": 3, "form": "output", "type": "float"},
        ])
        self.assertIsNone(plan["blocked"])
        self.assertEqual([(a["format"], a["write_mask"]) for a in plan["attachments"]],
                         [(80, 15), (80, 15), (10, 0), (10, 15)])

    def test_gap_is_an_inactive_zero_mask_attachment(self):
        plan = audit.plan_attachments([
            {"slot": 0, "form": "output", "type": "float4"},
            {"slot": 2, "form": "output", "type": "float2"},
        ])
        self.assertEqual([(a["format"], a["write_mask"]) for a in plan["attachments"]],
                         [(80, 15), (10, 0), (10, 15)])

    def test_integer_targets_are_an_explicit_surface_limitation(self):
        plan = audit.plan_attachments([
            {"slot": 0, "form": "output", "type": "uint4"},
            {"slot": 1, "form": "output", "type": "int4"},
        ])
        self.assertEqual(plan["blocked"], "bridge_attachment_format_surface")
        self.assertEqual(plan["blocked_slots"], [0, 1])
        self.assertEqual(plan["format_surface"], [10, 70, 80, 115])
        self.assertEqual(plan["attachments"], [])  # no fabricated float substitute

    def test_mixed_numeric_class_is_refused_per_slot(self):
        plan = audit.plan_attachments([
            {"slot": 0, "form": "output", "type": "float4"},
            {"slot": 0, "form": "input", "type": "uint"},
        ])
        self.assertEqual(plan["blocked"], "bridge_attachment_format_surface")
        self.assertEqual(plan["reasons"][0]["reason"], "mixed_numeric_class")

    def test_unknown_type_and_slot_range(self):
        unknown = audit.plan_attachments([{"slot": 0, "form": "output", "type": "struct"}])
        self.assertEqual(unknown["blocked"], "unknown_colour_target_type")
        far = audit.plan_attachments([{"slot": 9, "form": "output", "type": "float4"}])
        self.assertEqual(far["blocked"], "colour_attachment_slot_out_of_bridge_range")


class DescriptorDerivationTests(unittest.TestCase):
    def test_patch_control_point_descriptor(self):
        ll = ('!14 = !{i32 0, !"air.patch_control_point_input", !16, !17}\n'
              '!16 = !{!"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"float3"}\n'
              '!17 = !{!"air.location_index", i32 1, i32 1, !"air.arg_type_name", !"float"}\n'
              '!18 = !{!"air.patch", !"quad"}\n')
        plan = audit.plan_vertex_descriptor(ll)
        self.assertIsNone(plan["blocked"])
        self.assertEqual(plan["attributes"], [
            {"index": 0, "format": 30, "offset": 0, "buffer_index": 0},
            {"index": 1, "format": 28, "offset": 12, "buffer_index": 0},
        ])
        self.assertEqual(plan["layouts"][0]["step_function"], 4)
        self.assertEqual(plan["layouts"][0]["stride"], 16)

    def test_ordinary_descriptor_uses_per_vertex(self):
        ll = ('!10 = !{i32 0, !"air.vertex_input", !"air.location_index", i32 0, i32 1, '
              '!"air.arg_type_name", !"half2"}\n')
        plan = audit.plan_vertex_descriptor(ll)
        self.assertEqual(plan["attributes"][0]["format"], 25)
        self.assertEqual(plan["layouts"][0]["step_function"], 1)

    def test_merge_adds_missing_air_attribute(self):
        primary = {"attributes": [{"index": 0, "format": 31, "offset": 0, "buffer_index": 0}],
                   "layouts": [{"index": 0, "stride": 16, "step_function": 1, "step_rate": 1}]}
        fallback = {"attributes": [{"index": 2, "format": 28, "offset": 0, "buffer_index": 0}],
                    "layouts": []}
        merged = audit.merge_vertex_descriptor(primary, fallback)
        self.assertEqual([a["index"] for a in merged["attributes"]], [0, 2])


class ConstantPolicyTests(unittest.TestCase):
    def test_synthetic_values_are_labelled_and_not_silent(self):
        row = audit.choose_constant_value(
            {"index": 0, "name": "V3HLR::kXhlrbTileSize", "type": "ushort"},
            "canonical")
        self.assertEqual(row["value_hex"], "0100")
        self.assertEqual(row["origin"], "synthetic_diagnostic:canonical")
        alt = audit.choose_constant_value(
            {"index": 0, "name": "V3HLR::kXhlrbTileSize", "type": "ushort"},
            "alternate")
        self.assertEqual(alt["value_hex"], "0200")

    def test_descriptor_derived_attachment_constants(self):
        attachments = [{"format": 80, "write_mask": 15}, {"format": 10, "write_mask": 0}]
        count = audit.choose_constant_value(
            {"index": 6, "name": "fc_attachment_count", "type": "uchar"}, "canonical",
            attachments=attachments)
        self.assertEqual(count["value"], 2)
        self.assertEqual(count["origin"], "descriptor_derived:attachment_count")
        has0 = audit.choose_constant_value(
            {"index": 13, "name": "fc_has_attachment_0", "type": "bool"}, "canonical",
            attachments=attachments)
        self.assertEqual(has0["value"], True)
        has5 = audit.choose_constant_value(
            {"index": 18, "name": "fc_has_attachment_5", "type": "bool"}, "canonical",
            attachments=attachments)
        self.assertEqual(has5["value"], False)

    def test_unsupported_type_is_refused(self):
        with self.assertRaises(ValueError):
            audit.choose_constant_value(
                {"index": 0, "name": "x", "type": "pointer"}, "canonical")


class StageInputTests(unittest.TestCase):
    LL = r'''
!18 = !{i32 1, !"air.stage_in", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"float3", !"air.arg_name", !"position"}
!19 = !{i32 2, !"air.function_constant", !20, !"air.stage_in", !"air.location_index", i32 1, i32 1, !"air.arg_type_name", !"float3", !"air.arg_name", !"normal"}
'''

    def test_parse_stage_inputs(self):
        self.assertEqual(audit.parse_stage_inputs_ll(self.LL), [
            {"index": 0, "type": "float3"},
            {"index": 1, "type": "float3"},
        ])

    def test_stage_input_descriptor(self):
        descriptor = audit.metal_stage_input_descriptor(self.LL)
        self.assertEqual(descriptor["attributes"], {
            "0": {"format": "Float3", "offset": 0, "buffer_index": 0},
            "1": {"format": "Float3", "offset": 12, "buffer_index": 0},
        })
        self.assertEqual(descriptor["layouts"]["0"], {
            "stride": 24, "step_function": "ThreadPositionInGridX", "step_rate": 1})
        self.assertEqual(descriptor["index_type"], "UInt16")
        self.assertIsNone(audit.metal_stage_input_descriptor(""))


class BridgeOwnershipTests(unittest.TestCase):
    def test_zero_constant_specialization_is_an_owned_clone(self):
        here = Path(__file__).resolve()
        work = here.parents[5]
        corpus = work / "gpu" / "build" / "ui-corpus-20261025"
        bridge = audit.configure_bridge(corpus / "baseline" / "libtegu_air_bridge.so")
        raw = (corpus / "air" /
               "b5b1f4bfb77f02b47165d3e625befc1621b4e6fcf8f8f7dd2bc93ed921014f38.air"
               ).read_bytes()
        error = audit.C.create_string_buffer(2048)
        base = bridge.tegu_air_library_open(raw, len(raw), error, len(error))
        self.assertTrue(base, error.value.decode(errors="replace"))
        clone = audit.specialize_library(
            bridge, base, "path_blit_vert_lpf", [], error)
        self.assertTrue(clone, error.value.decode(errors="replace"))
        self.assertNotEqual(clone, base,
                            "count=0 specialization must return an owned clone")
        bridge.tegu_air_library_close(clone)
        bridge.tegu_air_library_close(base)


class PriorityManifestTests(unittest.TestCase):
    def test_source_tiers(self):
        self.assertEqual(audit.source_class("/System/Library/CoreServices/SpringBoard.app/x.metallib"),
                         "springboard")
        self.assertEqual(audit.priority_tier("/System/Library/CoreServices/SpringBoard.app/x.metallib"), 0)
        self.assertEqual(audit.source_class("/usr/libexec/backboardd"), "backboard")
        self.assertEqual(audit.priority_tier("/System/Library/Frameworks/QuartzCore.framework/default.metallib"), 1)
        self.assertEqual(audit.priority_tier("/System/Library/PrivateFrameworks/RenderBox.framework/default.metallib"), 1)
        self.assertEqual(audit.priority_tier("/System/Library/PrivateFrameworks/VectorKit.framework/default.metallib"), 1)
        self.assertEqual(audit.priority_tier("/System/Library/PrivateFrameworks/Espresso.framework/default.metallib"), 2)
        self.assertEqual(audit.priority_tier("/System/Library/VideoProcessors/NRFV3.bundle/default.metallib"), 3)

    def test_origin_ranking_does_not_trust_negated_text(self):
        synthetic_job = {
            "pairing_evidence": "abi_candidate",
            "descriptor_evidence": ("synthetic tightly packed buffer0 inferred from "
                                    "declared AIR inputs; not captured runtime state"),
        }
        self.assertEqual(audit.case_descriptor_origin(synthetic_job), "inferred")
        captured_job = {"pairing_evidence": "captured_fixed_ui",
                        "descriptor_evidence": "existing captured fixed UI constants"}
        self.assertEqual(audit.case_descriptor_origin(captured_job), "captured")
        self.assertEqual(audit.joint_origin_rank("synthetic", "captured"), "inferred")
        self.assertEqual(audit.joint_origin_rank("captured", "synthetic"), "captured")
        self.assertEqual(audit.joint_origin_rank("synthetic", "not_applicable"), "synthetic")

    def test_real_remaining_manifest_counts_and_priority(self):
        here = Path(__file__).resolve()
        work = here.parents[5]
        corpus = work / "gpu" / "build" / "ui-corpus-20261025"
        evidence = here.parent / "evidence"
        current = audit.current_source_paths()
        with tempfile.TemporaryDirectory() as tmp:
            summary = audit.build_priority_manifest(
                corpus, evidence, Path(tmp),
                current["compiler"], current["bridge"])
            self.assertEqual(summary["remaining_graphics_fc"], 273)
            self.assertEqual(summary["remaining_compute_fc"], 2875)
            self.assertEqual(summary["remaining_total"], 3148)
            self.assertEqual(summary["springboard_or_backboard_cases"], 0)
            self.assertEqual(summary["quartzcore_cases"], 4)
            self.assertEqual(summary["observed_entries_total"], 180)
            self.assertEqual(summary["observed_p0_rows_remaining"], 6)
            self.assertGreaterEqual(summary["observed_outside_remaining"], 170)
            rows = [json.loads(line) for line in
                    (Path(tmp) / "priority-manifest.jsonl").read_text().splitlines()]
            self.assertEqual(len(rows), 3148)
            self.assertEqual(rows[0]["observed_name"], "D8F18H")
            self.assertTrue(rows[0]["observed_priority"])
            self.assertEqual(rows[0]["priority_tier"], 0)
            self.assertTrue(all(r["remaining"] and not r["previously_executed"] for r in rows))
            self.assertTrue(all(r["app_coverage"] is False for r in rows))
            self.assertEqual(sum(1 for r in rows if r["synthetic_constant_values"]), 3148)


class CliPlanningTests(unittest.TestCase):
    def _job(self, same_library=True):
        return {
            "vertex": {"library_sha256": "same" if same_library else "v",
                       "library_saved_as": "libraries/l.metallib",
                       "name": "vfn"},
            "fragment": {"library_sha256": "same" if same_library else "f",
                         "library_saved_as": "libraries/l.metallib",
                         "name": "ffn"},
            "attributes": [{"index": 0, "format": 31, "offset": 0, "buffer_index": 0}],
            "layouts": [{"index": 0, "stride": 16, "step_function": 1, "step_rate": 1}],
        }

    def test_integer_targets_become_exact_cli_formats(self):
        plan = audit.plan_attachments([
            {"slot": 0, "form": "output", "type": "int4"},
            {"slot": 1, "form": "output", "type": "uint4"},
        ])
        cli = audit.cli_descriptor_plan(self._job(), "", plan)
        self.assertIsNone(cli["blocked"])
        self.assertEqual(cli["color_formats"], [
            {"slot": 0, "format": "RGBA32Sint"},
            {"slot": 1, "format": "RGBA32Uint"},
        ])
        self.assertEqual(cli["vertex_descriptor"], {
            "attributes": {"0": {"format": "Float4", "offset": 0, "buffer_index": 0}},
            "layouts": {"0": {"stride": 16, "step_function": "PerVertex", "step_rate": 1}},
        })

    def test_separate_libraries_cannot_use_single_library_cli(self):
        plan = audit.plan_attachments([{"slot": 0, "form": "output", "type": "uint4"}])
        cli = audit.cli_descriptor_plan(self._job(same_library=False), "", plan)
        self.assertEqual(cli["blocked"], "separate_source_libraries")

    def test_three_component_integer_has_no_metal_format(self):
        plan = audit.plan_attachments([{"slot": 0, "form": "output", "type": "uint3"}])
        self.assertEqual(plan["blocked"], "bridge_attachment_format_surface")
        cli = audit.cli_descriptor_plan(self._job(), "", plan)
        self.assertEqual(cli["blocked"], "no_metal_format_for_slot")


class ErrorClassificationTests(unittest.TestCase):
    def test_concrete_errors(self):
        cases = {
            "needs pipeline input: FunctionConstant { index: 41 }":
                ("missing_function_constant", 41),
            "needs pipeline input: VisibleFunctionTable { index: 0 }":
                ("missing_pipeline_input", None),
            "invalid descriptor: ColorAttachment { index: 2 }":
                ("colour_attachment_descriptor", 2),
            "invalid descriptor: VertexAttribute { attribute: 4 }":
                ("vertex_attribute_descriptor", 4),
            "invalid descriptor: VertexLayout { layout: 1 }":
                ("vertex_layout_descriptor", 1),
            "unsupported expanded render stage topology":
                ("bridge_stage_topology_refused", None),
            "attachment format or write mask outside bounded class":
                ("bridge_attachment_format_surface", None),
            "unsupported: Reflection (fixed_frag_lph_cpf); needs NotImplemented":
                ("translator_reflection_unsupported", None),
            "unsupported: Tessellation; needs NotImplemented":
                ("translator_tessellation_unsupported", None),
            "Intrinsic(SignatureMismatch) at bitstream bit 1":
                ("air_decoder_signature_mismatch", None),
        }
        for error, (cls, number) in cases.items():
            got = audit.classify_error(error)
            self.assertEqual(got["class"], cls, error)
            if cls == "missing_function_constant":
                self.assertEqual(got["index"], number)
            if cls in ("colour_attachment_descriptor", "vertex_attribute_descriptor",
                       "vertex_layout_descriptor"):
                self.assertEqual(got.get("attachment", got.get("attribute", got.get("layout"))),
                                 number)


class ResumeAndComposeTests(unittest.TestCase):
    def _core(self):
        row = audit.choose_constant_value(
            {"index": 0, "name": "x", "type": "ushort"}, "canonical")
        return {"schema": 1, "kind": "compute_fc", "case": "a",
                "compiler": {"compiler_sha256": "c1"},
                "initial_constants": {"kernel": [row]}}

    def test_identity_covers_value_and_compiler(self):
        core = self._core()
        one = audit._finalise_variant(core, {"family": "FunctionConstant"})["id"]
        two = audit._finalise_variant(json.loads(json.dumps(core)),
                                      {"family": "FunctionConstant"})["id"]
        changed = json.loads(json.dumps(core))
        changed["initial_constants"]["kernel"][0]["value_hex"] = "0900"
        three = audit._finalise_variant(changed, {"family": "FunctionConstant"})["id"]
        compiler = json.loads(json.dumps(core))
        compiler["compiler"]["compiler_sha256"] = "c2"
        four = audit._finalise_variant(compiler, {"family": "FunctionConstant"})["id"]
        self.assertEqual(one, two)
        self.assertEqual(len({one, three, four}), 3)

    def test_resume_requires_all_hashes(self):
        done = {"id1": {"variant_sha256": "v", "compiler_sha256": "c",
                        "bridge_sha256": "b"}}
        spec = {"id": "id1", "variant_sha256": "v"}
        self.assertTrue(audit._is_done(done, spec, "c", "b"))
        self.assertFalse(audit._is_done(done, spec, "c", "b2"))
        self.assertFalse(audit._is_done(done, {"id": "id1", "variant_sha256": "w"}, "c", "b"))

    def test_translation_survives_native_abort(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp) / "run"
            run.mkdir()
            (run / "translation.json").write_text(json.dumps({
                "status": "translated", "kind": "graphics_mrt",
                "specialization_status": "ok",
                "constants": {"vertex": [], "fragment": []},
                "descriptor": {"label": "declared_targets"},
            }))
            (run / "progress.json").write_text(json.dumps({
                "phase": "mali", "values": {"vertex": [], "fragment": []}, "attempts": [],
            }))
            spec = {"id": "x", "kind": "graphics_mrt", "case": "c",
                    "family": "color_attachment_format_or_MRT",
                    "descriptor_variants": [{"label": "declared_targets",
                                             "attachments": [], "attributes": [],
                                             "layouts": []}],
                    "baseline": {"classification": "audit_descriptor_insufficient"}}
            result = audit._compose_run_result(run, spec, -6, False, 0.25)
            self.assertEqual(result["translation_status"], "translated")
            self.assertEqual(result["compile_status"], "crashed")
            self.assertEqual(result["outcome_class"], "translated_backend_abort")
            self.assertTrue(result["descriptor_materialised"])

    def test_pre_translation_crash_retains_attempted_constant(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp) / "run"
            run.mkdir()
            (run / "progress.json").write_text(json.dumps({
                "phase": "specialize_fragment",
                "values": {"fragment": [{"index": 41, "name": "EnableDebug",
                                         "type": "bool", "value_hex": "01",
                                         "origin": "synthetic_diagnostic:canonical"}]},
                "attempts": [{"round": 1, "translation_error":
                              "needs pipeline input: FunctionConstant { index: 41 }"}],
            }))
            spec = {"id": "y", "kind": "graphics_fc", "case": "c",
                    "family": "FunctionConstant",
                    "descriptor_variants": [{"label": "job_descriptor",
                                             "attachments": [], "attributes": [],
                                             "layouts": []}],
                    "baseline": {"classification": "required_pipeline_input_missing"}}
            result = audit._compose_run_result(run, spec, -11, False, 0.1)
            self.assertEqual(result["outcome_class"], "worker_abort_before_translation")
            self.assertEqual(result["specialization_status"], "crashed")
            self.assertEqual(result["constants"]["fragment"][0]["name"], "EnableDebug")
            self.assertEqual(result["attempts"][0]["round"], 1)


class LedgerTests(unittest.TestCase):
    def test_ledger_preserves_entries_exclusions_and_carriers(self):
        with tempfile.TemporaryDirectory() as tmp:
            corpus = Path(tmp) / "corpus"
            corpus.mkdir()
            rows = [
                {"name": "k1", "stage": "kernel", "status": "backend_abort",
                 "air_sha256": "a1"},
                {"name": "g1", "stage": "vertex", "status": "bounded_attempt_killed_or_timeout",
                 "air_sha256": "a2", "attempts": ["x"]},
            ]
            (corpus / "coverage.jsonl").write_text(
                "".join(json.dumps(r) + "\n" for r in rows))
            (corpus / "embedded-scan.json").write_text(json.dumps({
                "excluded_special_files": [{"source": "/x", "mode": 33188, "bytes": 3}]}))
            (corpus / "other-formats.json").write_text(json.dumps({
                "libraries": [{"source": "/lib", "sha256": "s", "bytes": 1,
                               "status": "native_MachO_archive_parsed"}],
                "entry_counts": {"/lib": 2}}))
            (corpus / "graphics-jobs.json").write_text(json.dumps({
                "jobs": [], "unpaired": [{"name": "u", "status": "not_paired"}]}))
            out = Path(tmp) / "out"
            summary = audit.build_ledger(corpus, out)
            self.assertEqual(summary["entry_rows"], 2)
            self.assertEqual(summary["extraction_exclusions"], 1)
            self.assertEqual(summary["other_carriers"], 1)
            self.assertEqual(summary["unpaired_graphics_entries"], 1)
            self.assertEqual(summary["cpu_limit_unknowns"], 1)
            records = [json.loads(line) for line in (out / "ledger.jsonl").read_text().splitlines()]
            kinds = [r["record"] for r in records]
            self.assertEqual(kinds.count("entry"), 2)
            self.assertEqual(kinds.count("cpu_limit_unknown"), 1)
            self.assertEqual(kinds.count("extraction_exclusion"), 1)
            self.assertEqual(kinds.count("other_carrier"), 1)
            self.assertEqual(kinds.count("unpaired_graphics_entry"), 1)


class CliSelftestTest(unittest.TestCase):
    def test_pure_selftest_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = subprocess.run(
                [sys.executable, str(TOOL), "selftest", "--output", tmp],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            self.assertIn("SELFTEST PASS", proc.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
