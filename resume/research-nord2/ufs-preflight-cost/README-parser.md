# Offline20-line preflight report

Run `python3 research/nord2/ufs-preflight-cost/parse-preflight.py CAPTURE.log`.
Exit0 means complete, internally consistent evidence; inspect `success` separately.
A fully observed gate refusal is valid evidence and never success. Exit2 means
missing/corrupt/inconsistent evidence. No hardware access occurs.

Wraps the prior seven-line parser, then requires exactly those seven lines plus
PREFLIGHT, four GATE rows and eight OWN rows in order. Full row fields, LF/CRLF
termination, unique labels, no interleaving, bounded lengths and integer widths
are enforced. Raw bytes remain recoverable as raw_base64; raw_text, SHA256,
line-numbered evidence and prior parser output are retained even on failure.

Validity masks must be contiguous from bit0, stop at first failure and agree
with failure index and observable predicate values. Clear bits decode as UNREAD
with null value, never an observed zero. The first rejecting engine and nested
ownership predicate are named symbolically. Ownership context equality values
12..15 are reported but cannot be independently checked because their expected
context was not logged. No guessed expected address or partition bounds added.

Preflight diagnostics cannot establish link success. `success` additionally
requires the prior full x2 validation/negotiated x1 rollback report. Nothing
infers throughput gains or boot wall savings. As with any plain-text protocol,
rewriting a numeric value and supplying a new valid newline cannot always be
distinguished from authentic data; there is no on-device report checksum.
The parser rejects absent LF, clipped fields and known semantic contradictions.

`test-preflight.py` covers all14 initial engine outcomes, all23 ownership
refusals, symbolic diagnosis, observed zero versus unread, request order,
missing/duplicate/truncated/interleaved rows, masks, overflow, invalidUTF8,
SHA preservation and success delegation. Canonical report label check included.
No active kernel source/CI/device changes.
