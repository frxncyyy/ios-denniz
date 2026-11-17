---
name: nord2-delegate-analysis-to-fable
description: "Nord2/ios-sweet camera debugging — user wants new problems analysed by a Fable 5.2 subagent, not by the main session"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-10T13:08:17.895Z
---

On 2026-11-10, during the Nord2 camera bring-up, the user said (Italian): "ovviamente, per analizzare i nuovi problemi, delega sempre ad un subagente fable 5.2". Earlier the same day they asked for "un agente fable 5.2 a medio" (medium effort).

**Why:** the user wants root-cause analysis (disassembly, log reading, comparing with reference projects) done by a Fable 5.2 subagent each time a new problem shows up in a run.

**How to apply:** in that project, when a run exposes a new failure, spawn (or continue) a Fable 5.2 agent at medium effort with the log paths and facts, instead of analysing in the main session. My interpretation: the main session still builds images, runs the flash transactions and reports; mechanical steps (reading a log to decide what to hand over) stay with me. See also [[nord2-cpu-limits-lifted-for-builds]].

**Update 2026-11-12:** the Fable limit ran out around 03:50 (subagent failed with HTTP 429 "You've reached your Fable limit"). From then on I used Opus subagents for analysis (the user had also asked for Opus agents during the overnight run). Try Fable first again, and fall back to Opus on a 429.
