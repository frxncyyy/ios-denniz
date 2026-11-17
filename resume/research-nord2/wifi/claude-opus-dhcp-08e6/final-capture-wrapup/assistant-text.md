Both addenda are already read (late GTK works; no matched DHCP; MIC alone is not PTK-RX proof; rate 0 valid; shared PID127 ambiguity). Now making the test-only negative-control tightening.

Now tightening `run.py` so only the exact marker plus exit 77, with no sanitizer output, counts as discrimination.

Now the wrapup addendum and changelog.

The negative control is now tightened so only the intended STA-BMC mismatch counts as a passing baseline result. I couldn't execute anything, so neither the test nor the runner changes have been compiled or run.

**Conclusion after the final capture:**
- Late GTK renewal works on hardware, so no rekey change is needed. MIC success alone still doesn't show the frame arrived PTK-encrypted.
- There is still no matched DHCP.
- The STA-record BMC = 255 fix stays source-proven and unchanged. Whether it affects DHCP is unproven until the next hardware run.

**Test-only changes:**
- **`tests/test-sta-bmc.c`:** a new `parity_probe()` runs before any other check. It looks at the STA BMC the codec actually serializes and the one native `peer_progress` STA1 actually publishes:
  - **Source parity** (stale 4 rejected, native 255): prints a parity marker and continues the full positive suite (STA1/STA3 = 255, BSS = 4, GTK slot, protected TXD, M2/M4/G2 policy, retire, fault injection).
  - **Expected baseline mismatch** (stale 4 accepted and serialized as 4, native STA1 = 4): prints one exact mismatch marker, cleans up so LeakSanitizer stays quiet, and exits **77**.
  - **Anything else:** aborts, and never exits 77.
- **`tests/run.py`:** the baseline now counts only if it compiled, exited exactly 77, and its last stdout line is the mismatch marker, with no sanitizer text. The candidate must exit 0 with the parity marker, a `PASS` last line and no sanitizer text. Any other signal, assertion, sanitizer exit or nonzero code fails the suite.

Per your nine-group station pass, no fixture migration was written and canonical fixtures are untouched. `candidate/edits.py`, `candidate/candidate.diff`, `initial-freeze/` and earlier reports are unchanged.

**Parent requirement:** rerun `python3 -I research/nord2/wifi/claude-opus-dhcp-08e6/tests/run.py --log-dir <new dir> --regressions` and confirm `candidate_ok` and `baseline_expected_mismatch` are true, both oracles pass, and the station, normal-tx and gtk-rekey regressions pass.

Files are in `research/nord2/wifi/claude-opus-dhcp-08e6/`:
- tests/test-sta-bmc.c
- tests/run.py
- final-capture-wrapup/CHANGELOG.md
- final-capture-wrapup/ADDENDUM.md
