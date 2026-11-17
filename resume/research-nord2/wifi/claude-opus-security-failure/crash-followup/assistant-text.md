All 8 reports share one identical signature. Now the summary (exception reason text, if captured).

Proceeding with narrow reads only: the wifid disassembly around the return address 0x1000930e4.

The crashing call is at `0x1000930e0` (`bl 0x100175510`), return address `0x930e4` = the reported top wifid frame. This region is stripped (nearest export is a thunk), so the function/selector aren't symbolized here. Let me characterize the three caller frames briefly.

The caller at `0xf119c` is an indirect handler dispatch (`blr x8`) from a libdispatch block. Let me check whether the extracted report body preserved the Objective-C exception **reason** string (value-vs-key, and queue label), which is the deciding field.

You've hit your session limit · resets 9:30pm (Europe/Rome)
