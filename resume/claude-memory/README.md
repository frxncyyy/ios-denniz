# Claude project memory (resume context)

These are the assistant's project memory notes from the author's host (2026-11-10 .. 2026-11-16), copied so that a
Claude session on another PC can pick up the same standing rules and history. Session ids and host paths were
removed; nothing else was changed. To use them, copy the files into the new host's Claude project memory directory
(`~/.claude/projects/<project>/memory/`) and keep `INDEX-original-MEMORY.md` as `MEMORY.md`.

| Note | Kind | What it says |
| --- | --- | --- |
| [nord2-phone-test-procedure.md](nord2-phone-test-procedure.md) | procedure | txn / log-reader / auto-lock / battery-guard pitfalls on the phone |
| [nord2-phone-tap-scenarios-need-screenshot-check.md](nord2-phone-tap-scenarios-need-screenshot-check.md) | standing rule | screenshot before every tap or swipe; no fixed-coordinate scenarios |
| [nord2-cpu-limits-lifted-for-builds.md](nord2-cpu-limits-lifted-for-builds.md) | standing rule | CPU quota may be dropped for compilations; thermal guard stays |
| [nord2-delegate-analysis-to-fable.md](nord2-delegate-analysis-to-fable.md) | rule (superseded) | analysis delegation; later superseded by the Opus-only mandates |
| [nord2-night-mandate-20261114.md](nord2-night-mandate-20261114.md), [nord2-night-mandate-20261115.md](nord2-night-mandate-20261115.md) | mandates | full-autonomy nights, Opus-only agents, reports at 08:00 |
| [nord2-haptic-final-6a.md](nord2-haptic-final-6a.md) | result | haptic final kernel q10hapd6a (pieces any combined image must carry) |
| [nord2-gnss-working.md](nord2-gnss-working.md) | result | first GNSS fix, root causes |
| [nord2-night-20261115-results.md](nord2-night-20261115-results.md) | result | q10jfinal8a + gfxall11 state; what to never combine (0664/0667) |
| [nord2-jbts1-sileo-ssh-trollstore.md](nord2-jbts1-sileo-ssh-trollstore.md) | result | jbts payload (SSH over Wi-Fi only) |
| [nord2-files-on-my-iphone-fixed.md](nord2-files-on-my-iphone-fixed.md) | result | q10jfinal44ts: 0732 + 0734, how to undo |

Later facts (2026-11-16/17, not in these notes): Bluetooth pairing + AAC stable in transport mode 2; SIM and modem
firmware restored (0715 + nvfile, simcache); Terraria landscape + touch (gfxall17 + flip); torch/flash, voice calls,
SMS/MMS, front camera, 4K recording, proximity sensor and 5 GHz Wi-Fi verified by the user on 2026-11-17. See [../../docs/STATUS.md](../../docs/STATUS.md).
