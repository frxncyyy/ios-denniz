I'm reading the rest of the filtered trace to see which requests arrive around the join attempt and whether the client's log rate limits could hide them.

Scanning the full snapshot for non-Wi-Fi kernel lines (process exits, sandbox or AMFI denials) near the repeated client re-opens.

I'll keep the remaining reads narrow (Wi-Fi driver lines and launchd service-exit notices only) and won't reproduce unrelated log content.

# Nord2 iOS16 Wi-Fi: why Join still fails before SET20 (image 6e4ce860)

**Short answer:** I found no definitive kernel code defect that explains the missing SET20. The trace proves the association request never reached the driver, and the failure starts at a user-space step I can't see from kernel logs. The most informative next evidence is on the phone already: user-space crash reports and logs. This needs no new build or flash. Below is what is verified, what is likely, and what to extract.

(My previous reply was cut off while it was listing general boot log lines; I'm not repeating that and only quote driver-relevant facts here.)

## 1. Verified from the trace and current source

**SET20 truly never reached the client.** Missing log lines are not a rate-limit artefact:
- `JOINREQ` prints before decode in `joinLocked()` (`wifi_ios_client.cpp:363`), limited only by `joinLogs<32`. Only join paths increment `joinLogs`, and every client starts at zero.
- `JOINCONTROL` goes through `traceControl()`, which allows 40 distinct request/operation pairs and 8 lines each, per client. Each client is new and uses about 25 pairs, so the first SET20 or SET22 would always print.
- The only paths that could hide a SET20 are the silent early returns in `dispatchLocked()` (`wifi_ios_client.cpp:111-128`): closed phase, wrong task, bad argument header, or an input size that isn't one of the two fixed carriers. Real Apple80211 SETs use the same fixed carrier as SET19, which does reach logging. So a hidden SET20 is very unlikely, though not strictly impossible.

**The CARD fix is executing.** Every GET12 returns byte0=0x08, byte1=0x40. The capability gates before SET20 that we analysed earlier are therefore satisfied.

**The behaviour changes at about t=138 s; before that the stack looked healthy.**
- **Boot (26–41 s):** five clients run the device bring-up. At 31.4 s the client sequence 43, 12, 96, 97, then event-pipe selectors 5/3/2 completes within about 3 ms. Scans then work (90.9–95.4 s) and the system goes quiet.
- **From 138.1 s:**
  - The same 43/12/96/97 client now waits about 5.1 s between the GET97 refusal and selector 5. This happens at 138.1→143.3, 153.8→158.9, 169.3→174.4, 235.7→240.8, 251.2→256.3 and 266.7→271.8 s. The driver answers instantly; the wait is in user space.
  - A new client at 143.3 s sends GET1 (current SSID, ENXIO) twice, then **GET51 (country code, 3 bytes)**, which is refused as unsupported, then GET27. GET51 never appeared during boot and recurs after this point.
  - The full device bring-up client pair (43, 12, 19, 106, 254, 207, 103×3, a SET19 power-off attempt, then the 383/18/5/13…/25 batch) **repeats roughly every 15 s**: 153.5, 169.1, 184.6, 250.9, 266.4 and 281.9 s.
- **What this suggests:** at boot that bring-up ran once. Repeating it every ~15 s strongly suggests a Wi-Fi daemon is restarting or re-creating its device after the user's action. A 15 s period is consistent with a crash followed by launchd's respawn throttle. That would explain both the ~10 s delay before the password sheet (two ~5 s stalls) and "unable to join" without any SET20.
- **This is a hypothesis.** Nothing in the kernel log records a process exit (the launchd console output stops after early boot), and our client doesn't log `clientDied` versus `clientClose`.

**Not the cause:**
- The refused SET19 power-off (state=0) also happened at 41.6 s, and scans worked afterwards.
- The other refused GETs (96, 97, 106, 207, 18, 5, 14, 8, 57, 17, 6, 115, 383, 25, 365) all appear identically during the healthy boot phase.

## 2. Ranked hypotheses

**H1 (most likely): a user-space crash or abort loop triggered when a network is selected.**
- The 15 s re-initialization cadence and the new 5 s stalls begin together at 138 s.
- **Test it without a flash:** read the phone's existing crash reports and logs for the Wi-Fi daemon and for Settings/Wi-Fi helper processes around boot+138 s, using the existing read-only collection path. A crash signature or abort reason will name the failing function and decide this directly.

**H2: the join path needs a country code (GET51) and treats "unsupported" as fatal or as an indefinite wait.**
- GET51 is the only request type that first appears in the post-tap window. It sits right between the SSID check and the supported-channels query, which looks like a pre-join regulatory step.
- If confirmed, the fix is small: answer GET51 from the native regulatory owner.
- Caveat: the efuse/board country is currently 0, so the honest answer is the world code "00". Whether iOS accepts world mode for joining needs the binary evidence below. Do not invent a country.

**H3 (low): SET20 is silently refused by one of the `dispatchLocked` early returns.** As explained above this is very unlikely; one cheap counter would rule it out (fix 4).

## 3. Binary evidence for the root to extract (exact targets)

1. **In the same-IPSW Wi-Fi daemon:** the RawGet/Apple80211 caller that issues request 51 (country code) and its failure branch. The internal enum stores at `0x1000751ec`, `0x100054624`, `0x1000546f0` and `0x100174808` in `initial-query-audit/wifid-disassembly.txt` load the value 51, but they are internal XPC/queue codes, not the IOCTL itself. Locate the actual `Apple80211Get(…, 51, …)` call and follow its error result up to the join/network-selection caller.
2. **The caller that issues GET96 then GET97 then opens the event pipe,** and what it waits on for about 5 s (a semaphore or XPC timeout). This tells us which service it is waiting for.
3. **In the selected framework:** confirm whether any association entry point other than `0x1d3ebf830` (the only request-20 builder we validated; there are other `mov #20` sites such as `1d3ebd9a4`, `1d3ec56b4`, `1d3ec6110`, `1d3ecf1cc`) is the iOS 16 join path. Also check whether it is gated on country code or locale before its RawSet.

## 4. Minimal fixes, in order, only once the evidence supports them

1. **If H1 is confirmed:** fix whatever driver response the crash report points to. Don't guess beforehand.
2. **If H2 is confirmed:** implement GET51 in `getLocked()` (`wifi_ios_client.cpp`, next to request 43/44).
   - Validate exact shape/capacity like the other scalar GETs.
   - Return the 3-byte code taken from `nrp_country()` (the native regulatory owner) and refuse if the owner is unavailable.
   - Regression test: correct shape returns the native code, a short capacity is refused, and the reply never contradicts the channel list returned by GET27.
3. **Diagnostics to bundle with any build that is happening anyway, not worth a flash on their own:**
   - Log `clientDied` versus `clientClose` with the client's age. This separates a crash loop from normal reopening on the next attempt.
   - Add one counter for silent `dispatchLocked` early refusals (reason code plus request ID when decodable). This closes H3.

**Hardware status:** nothing here touched the radio. No association, key exchange, DHCP or IP has been attempted or observed on hardware yet.
