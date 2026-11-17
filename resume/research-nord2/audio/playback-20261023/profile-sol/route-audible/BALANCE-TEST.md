# Actual hardware balance regression

Canonical authorized edit: tests/nord2-audio/test-balance.c only. Private identical source and executable are stored here. Built against current parent's actual audio_hw/afe/id/i2c8/amp/board C implementation, strict C11 warnings and address/undefined sanitizers; executed with pinned4163B container. Result PASS.

56 successful paired0c74 lifecycle cases cover49gainpairs from0,128,256,362,512,724,1024 and7legacy wrapper gains. An independent signed int64 oracle compares every model DMA SRAM packed word during ramp and steady state. Both inputs alternate INT16_MIN/MAX; allchannel pairs test gains independently, including top-only/bottom-only. Starvation, queued zeros and input exclusively on a zero-gain channel do not consume ramp. Tests inspect exact perchannel peak counters, initial480 effectiveframes, bound/clamping arithmetic, stops preserve peaks/reset ramp, restarts reset peaks and fade fromframe1. Separate tests exercise malformed gains before any AFE/controller access, pointer fault during fade, and silent diagnostic draining without advancing fade.

Runner integration (parent owns tools/nord2-audio-check.py):
`cases.append(('balance',[T/'test-balance.c']+modules,[],[a.board.resolve()]))` inside existing `if a.board` block.

This establishes implementation behavior in the actual MMIO/amp transport model, not physical speaker protection or acoustic safety at the new ceiling.
