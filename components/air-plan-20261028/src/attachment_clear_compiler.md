# Exact attachment-clear compiler class

`tegu_mali_compile_attachment_clear_pair_v2` compiles the captured
`path_blit_vert_lph` / `attachment_clear_frag_lph` pair separately from the generic
v6/v7 compiler paths. The class requires logical RT0 BGRA8/write-mask0 and RT1
BGRA8/write-mask15. It preserves those indices.

The vertex compiler lowers one readonly rect StorageBuffer at set0/binding0 to
a bounded backend UBO descriptor, alongside the320-byte matrix UBO at binding1.
Its versioned resource plan retains source roles, descriptor indices, runtime
push-byte offsets, alignments and required access windows. The caller must
validate actual buffer ranges, including descriptor padding beyond a residual.
The vertex result has position/PSIZ programs and no secondary/varying program.
The fragment result requires float32 vec4 outputs at locations0/1, index0, and
separate blend-descriptor words for both logical targets.

This is an exact code-generation class. It checks both generated code hashes
against the independently tested pair and refuses different code, unsupported
resources, writes, output types and descriptor reindexing. It does not add a
frontend route, package dispatcher, render-pass support or a Metal capability.
The draw and attachment fields in the plan are requirements for the caller to
enforce; compiler success does not validate an actual Metal pass.

The header documents output ownership and invalid-span/alias refusal behavior.
Both compiler build scripts copy `compiler_trace_sha256.h` beside the source.
No shader or OS binaries are stored in this directory.

## Host check

Supply the qualified vertex/fragment SPIR-V pair and an authored texture fixture:

```sh
python3 tools/tegu/metal/check_attachment_clear_compiler.py \
  --library /path/to/libtegu_mali_attachment_clear.so \
  --vertex /path/to/path_blit.vert.spv \
  --fragment /path/to/attachment_clear.frag.spv \
  --texture-fragment /path/to/tex_ok.spv
```

The check compiles the actual pair, verifies exact output bytes and metadata,
and exercises resource/output mutations and input/output alias refusals. The
existing texture compiler checker independently compares legacy code generation.
The promotion passed43 report cases, including34 byte-identical legacy calls.

The same compiler source was built and linked locally as an iOS16 ARM64 dylib
and static archive; all128 dylib imports exist in the actual iOS16 donor stub.
Those artifacts were not executed on the phone. See the current `NOTES.md`
handoff for evidence paths, hashes and remaining driver integration work.
