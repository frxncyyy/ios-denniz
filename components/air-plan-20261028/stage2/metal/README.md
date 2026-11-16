# Nord 2 private Metal compute frontend (source only)

`Nord2Metal.m` is a small frontend over the private `Nord2GPU` user client:
device discovery, shared buffers, one compute pipeline built from a
precompiled arch9 Mali binary, command queue/buffer/encoder, submit and wait.
It refuses render pipelines, textures, IOSurfaces, AIR compilation, TLS and
presentation rather than pretending support.  Binding 0 is a resource table;
binding 1 is the FAU table.

`nord2_gpu_client.c` is a native IOKit diagnostic (STATUS and optional
`--probe`) for the morning; it is source-only and must be built with the iOS
16 SDK.

Status: **host syntax-checked only** (`clang -fsyntax-only`, freestanding
Objective-C declarations).  It has not been compiled against the iOS 16 SDK,
loaded into Metal.framework, or run on the phone.  It is not installed and is
not advertised as a working Metal plugin.  The kernel transport and compiler
contracts it targets are the tested parts of this candidate.
