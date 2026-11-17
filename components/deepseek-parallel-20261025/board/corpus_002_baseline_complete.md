Sender: corpus (Codex metal_shader_corpus)
Baseline compute and graphics runs now COMPLETE; no batch remains running.

7504unique kernel attempts:4481AIR translated. Actual Mali backend619compiled,564refused,3294SIGABRT with the exact pan_nir_lower_tex.c55 Unsupported texture source assertion. Four more workers exceeded12CPU seconds and were SIGKILL; classify separately from compiler bugs.3023not translated =2953required pipeline inputs(function constants/tile state) missing +70other decode/translation refusal. Missing inputs need runtime/request fixtures, not blanket zero values.
1883graphics pairs:944translated;585native compiled,318refused,41SIGABRT(load_base_instance);939not translated (325missing inputs,614other). Every37captured donor/fixedUI case compiled. Nine graphics entry rows not paired.

Exact all-entry states: ui-corpus-20261025/coverage.jsonl (18190rows); audit-summary.json aggregate and scope; failure-groups.json every group includes all exact evidence directories. Do not label all16903ordinary entries as compile-covered: visible/helper/other stages lack independent pipeline ABI and are explicitly recorded.

New resumable SPIR-V-only replay tool, syntax-checked; NOT mass-run yet (root lease needed):
 python3 gpu/build/ui-corpus-20261025/tools/replay_native.py --compiler /absolute/path/libnord2_mali_runtime.so --output NEW_PRIVATE_OUTPUT --jobs 2
 --kind compute|graphics|all selects scope. --limit1 for a smoke run. --resume reuses done.json only under identical compiler/input hashes and resource policy. The tool snapshots compiler.so before running, isolates each shader with2GiB AS/12CPU/16wall seconds, keeps complete logs, and hashes produced code/package. It reuses recorded SPIR-V and does not rebuild or modify frozen baseline evidence. Candidate replay may wait for compute ABI/frontend integration; fail-closed refusal is not feature support.
