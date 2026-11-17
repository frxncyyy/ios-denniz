# Completed resumed Claude review — hardware b8851761

Same session01e36598-017d-44c6-9f31-18c35b9ccb96 completed successfully, process exit0. Exact restricted flags from the successful followup-command-data invocation were retained: safe/restricted, dontAsk/no prompts, only Read/Grep/Glob, strict empty MCP configuration and selected Android source directory. Observed tool use:12 Read,16 Grep; no shell/write tools. A real read-only foot viewer was confirmed on workspace3. Earlier failed Lua viewer calls are preserved; working call uses hl.dispatch(hl.dsp.exec_cmd(command,{workspace="3"})).

Prompt and immutable inputs are pinned in input-manifest.json. Claude's complete final report is preserved verbatim as REPORT.md; do not silently normalize its reasoning into proved facts. Its principal conclusion is no source-supported functional TX correction yet. It withdraws the old COMMAND-layout-invalid claim, does not attribute the unknown late drop to rekey, and identifies TX PTK/application, radio retry, silent unicast RX rejection, optional TXS/report policy and command-data prerequisite as unresolved alternatives.

## Codex qualification of the final report

- Shared PID127 prevents a definitive claim that every error belongs to protected ordinary data, or that no delayed M4 report is among later events. Successful M3 proves M2 reached the AP; absence of early TXS does not establish the exact policy for every later frame. Sequence gaps also are not proof of successful frames.
- Count30 is numeric firmware metadata matching retry policy; it is not proof of30 on-air transmissions. TX delay would add evidence, but is not automatically an exclusive air-retry versus firmware-reject discriminator without its selected-source validity/units/queue semantics.
- LIFE_TIMEOUT is a separate status enum, so current status3 provides no affirmative basis for a lifetime change; wording that lifetime is categorically ruled out should not exceed that evidence.
- Reported accepted group CCMP RX does not independently prove live PTK-TK correctness. Separate tiny current-engine/BearSSL sanitizer check under independent-tk-check verifies full48-byte deterministic PTK oracle including TK32..47, but proves neither live inputs nor native hardware installation.
- Proposed additional fields/counters or PID domain changes are suggestions for review, not approved patches. Parent/wire/cap retain source review and ownership. No source or descriptor acceptance change is authorized by this report alone.

No canonical files, build, CI, device/USB, gates or firmware were changed. The only launcher exception was the expressly requested read-only viewer. The live collector was not accessed.
