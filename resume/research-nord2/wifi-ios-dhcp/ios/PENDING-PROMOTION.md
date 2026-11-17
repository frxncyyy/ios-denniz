Pending metadata handoff

The pending JSON files rehash 335 existing/current canonical source and test paths, including 0082, STA sentinel changes and RXERROR reader/fixtures. No prior image/source approval is transferred. Local verification pins include the completed 16-unit compile and root test reports. DHCP is not proven.

When CI_READY arrives, the parent must:

1. Compare every pending source pin with the exact committed source, not just the working tree. Stop on mismatch and refresh/review changed pins.
2. Record the successful new CI run/attempt/head, complete artifact run.json, actual kernel path/SHA256/length. Do not reuse any predecessor identity.
3. Complete generated-source equality and actual compile-flag audits and the bounded linked review for STA state1/state3 BMC255, BSS4 preservation and the RXERROR observation call before unchanged rejection. Pin review.json and manifest.json plus exact kernel/source identity.
4. Refresh collector/reader dependency pins from cap's completed stage review, including the RXERROR strict reader. Require RXERROR marker availability in the artifact; absence in a hardware log is not proof of error-free reception.
5. Only then promote pending source proposals and schema to their required filenames, update candidate_manifest_path/hash consistently and set parent schema/identity verification based on those new proofs. Keep prepared image and human authorization false until their separate actual checks/approval. Image generation and device actions remain parent-only.

The original 8882 positive STA test is the durable canonical fixture; no migration or rerun of the unpromoted final marker experiment is required.
