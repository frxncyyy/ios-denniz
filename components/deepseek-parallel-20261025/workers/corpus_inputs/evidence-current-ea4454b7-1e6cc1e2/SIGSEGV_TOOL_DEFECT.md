# Tool-defect reclassification

The `worker_abort_before_translation` rows for graphics-00086, graphics-00498 and
graphics-01278 in this directory are an **audit-tool use-after-free**, not an
air_bridge defect.

Cause: `specialize_library` returned the base library handle when the supplied
constant list was empty. On a missing-function-constant retry, `worker_main`
closed every handle including that base, then reused the freed base pointer.
The Rust bridge's zero-constant specialization returns an owned clone.

Fix: always call `tegu_air_library_specialize`, including count=0, and close only
that returned handle. Production `library_frontend.inc` was independently
verified by compute/root as unaffected.

Re-run with the fixed tool against compiler ea4454b7 + bridge 8861e0a0:
`evidence-current-ea4454b7-8861e0a0/sigsegv_rerun` gives no signals:
- graphics-01096, graphics-01229 native compiled
- graphics-00086, graphics-00397, graphics-00424, graphics-00498 native refused
- graphics-01278 explicit VertexAttribute{4} descriptor refusal
These replace the old SIGSEGV outcomes. All constants remain synthetic; no app
coverage or hardware claim.
