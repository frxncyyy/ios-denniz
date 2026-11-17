---
name: nord2-phone-tap-scenarios-need-screenshot-check
description: "Driving the phone with fixed-coordinate taps opened the wrong app (Clock); user objected to \"operazioni a caso\""
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-15T00:05:11.201Z
---

On 2026-11-15 ~01:05 the user (awake, watching the phone) objected: "stai facendo operazioni a caso... hai aperto l'app orologio". My phone.py scenario used fixed panel coordinates (e.g. tap 164 760 for Maps), but the App Library "Suggestions" folder is dynamic, so taps landed on other apps.

**Why:** the user watches the physical phone; blind taps look random and are unwanted even under the full-autonomy mandate ([[nord2-night-mandate-20261115]]).
**How to apply:** before any tap scenario, take a screenshot, locate the icon, and tap from that; prefer launching apps by a stable route (dock, Spotlight) or log-only/screenshot-only observation; tell the user before driving the UI when they may be present.

**Update 2026-11-15 ~01:10:** user is leaving and authorises me to drive the phone myself, with one hard rule: from unlock onward take a screenshot BEFORE EVERY tap/swipe and decide from what it shows (also when preparing stress tests: rely on screenshot data). No fixed-coordinate scenario files without a per-step screenshot check.
