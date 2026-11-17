# Thermal-resume override — 26 October 2026

The human requests lower CPU usage after a thermal shutdown. Resume the same
Claude session0549114f-d223-4e6c-bedc-9119774ffaf5, preserve its latest work.
Read $NORD2_REPO/research/nord2/gpu/overnight-20261024/claude-metal-orchestrator-20261025/runtime/resume-20261026/RESUME.md.
All workers/tool children stay in nord2-metal.slice, aggregate CPUQuota75% of
one core maximum, CPU affinity0,1; thermal guardian reduces to25% at78°C and
freezes at85°C until70°C. Do not bypass/relax these limits. Max2 DeepSeek workers
and1 Claude subagent concurrently, one CPU-heavy job globally (including test/
replay), all jobs1. Rotate roles at checkpoints. deepseek_control.py enforces
the external worker cap and slice. No phone operations for this resume.

# Active Nord2 Metal orchestration

The human transferred this project to this interactive Claude Code session. Read
$NORD2_REPO/research/nord2/gpu/overnight-20261024/claude-metal-orchestrator-20261025/HANDOFF.md first, then AGENTS.md and the newest NOTES.md.
Main model Opus5.5 medium. Use native project agent profiles and the existing five
OpenCode DeepSeek v4.1 Flash max sessions via the shared board; preserve exact file
ownership and one-build lease. No Codex production writers remain. Do not publish
private tegu-derived source or access the phone without new exact-artifact approval.
Continue local implementation and verification autonomously; deadlines are targets,
not proof of completion. Keep source, host tests, native build and hardware results
distinct. Main integrates; use cheap workers for implementation and Sonnet high for
most Claude tasks, Opus high only for hardest compiler/lowering issues.
