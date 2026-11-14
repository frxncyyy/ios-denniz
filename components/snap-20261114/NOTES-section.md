
## snap-20261114: 0640 overlay-mode screenshot + panel/brightness/HID status (image q10jcx, NOT device-tested)

Why the 0296 tools went blind [V-code]: 0416 replaced sweet_fb_snap() by `return NULL` (no CPU read of the panel), so every
screenshot and every verb that looks at the screen (wake/sleep/launch/mode...) failed with result 4. `panel_off` in the status was
only ever the snapshot's info word 7 (written by a successful picture), so it stayed 0 whatever the panel did. The log text
"SWEET FB: display off" is pre-0416; today's line is "NORD2 FB BLANK: DCS display off sent: panel dark" (vsync thread, DCS 0x28)
and "NORD2 FB BLANK: DCS display on sent" (0x29). Injected fingers 8/9 go through sweet_hid_touch() like the FT3518 ones
(SweetHID touch() logs "touch N down" for any finger 0..9) - why those lines were not seen in the log is not known (live log full /
reader offset are candidates); the user saw the touches and the power key act, so delivery was never broken.

0640 (snap-20261114/0640-nord2-overlay-snapshot.patch, on the q10jcq stage; files SweetFramebuffer.cpp, new sweet_fb_snapc.h,
SweetSurface.cpp, hid/SweetHID.cpp, bsd/dev/nord2_autotest.inc):
- Screenshot = the plan on the panel composed in software, on USB request only (nothing per frame; display stays CPU-free).
  presentDone copies the planned layers' device ranges + surfaces next to fHwShownPlan (under fLock; a surface not in fHwHeld is
  never named). snapHw copies plan/ranges and retains the surfaces under fLock, drops it, composes. Composition = the register plan
  slot by slot per pipe (2L L0, L1, 4L L0..3 over black), CON decoded with the 0399 colour model, AEN/plane alpha straight (pm 0)
  or premultiplied (MAN), packed 4:2:2 via sweet_fb_yuv_ref_rgb + CLIP; the 0550 band pieces are plan slots, so the rotated band
  comes out as the panel shows it. Unreadable/unmodelled slot -> MAGENTA rectangle + flag "marked-magenta" (never guessed).
  Pixels read by sweet_surface_snap_read(): only mappings the driver already has (buffer, pool, heap, purge sink, existing
  kernelMap), else IOMemoryDescriptor::readBytes under a non-blocking wod hold; cacheable ranges CIVAC'd first. No new mapping,
  no ml_io_map. Kill switch sysctl kern.n2snap=0 (refusal 20); 21 = no overlay frame shown yet; 22 = no memory (7.8 MB buffer once).
- Status (wLength >= 128, old 80-byte answer unchanged): words 20..31 = panel flags (iOS display-off request, DCS display off
  sent = dark, overlay on, band rotation live, last snapshot flags/ms), DBV sent | target, brightness request nits 16.16, presents,
  HID dispatch counters (injected touch reports, injected downs | key reports, all touch reports, touch/button clients, last
  injected x,y, injected reports with no client), magic 0x534e0001. Lock-free racy reads.
- SweetHID: injected touches log "SWEET HID: injected touch N down|up at X,Y (C clients)"; counters as above.
- phone.py (autotest/phone.py; old = phone.py.pre-night): status parses the new words (panel_off = real state), new verbs `panel`,
  `launch app X Y`; wake/sleep/home/unlock use the status panel state (fallback: picture); tap/key print the HID delivery delta;
  screenshot JSON has slots/flags/compose_ms; refusal messages 20..22.
- Host tests (snap-20261114/test): t_snapc.c = compositor vs an independent per-pixel reference on random plans (1..6 layers, alpha
  0/255/random, plane alpha, pm 0/2, csw, 4L cascade, 0551 band rotation = reference at the inverse x, YUV with odd edges and Cb/Cr
  swap, unreadable layers, scale/crop, RLE round trip; fixed: addresses outside sources, unknown CFMT, exact range edge, corrupt
  geometry) under ASan/UBSan, mutation-checked (7 mutants caught). t_proto.py + t_proto_lib.c = the real nord2_autotest.inc +
  compositor in a shared library driven through phone.py's own Phone methods and verbs (status framing 80/128, pictures for 6
  scale/crop/packing cases equal to the sampled full picture, refusal text, wake/sleep/panel/tap/key/screenshot/launch app/home).
- Found while testing: with kern.n2fbhwcsw != 0 the 0550 band pieces of layer 0 take the UPPER-slot colour swap (debug knob only;
  default 0; 0551 has the rotation off by default).
- Image q10jcx = q10jcq recipe (MORE_PATCHES ... 0540 0550 0551) + 0640, MORE_DIRS + $B/snap-20261114 (script
  snap-20261114/build_q10jcx.sh, SCANPATH_EXTRA_ALLOWED + _nat_thread _nat_usb): sha256
  bf2bf8b5dd5a9b6e523ffeadf975c0a686e04d74630bdf0741e96636f560c82b, 15151104 B, check.log FAILURES: none, SCANPATH_OK,
  WIFI_MEMBERS_OK, the known 0399 IMAGECHECK stop; compile warnings unchanged vs q10jcq (FB 1438, Surface 1555, HID 1331, touch 0).
- To read on the phone: `phone.py panel` (panel.on/dark, brightness.dbv_sent), `phone.py screenshot a.png --scale 4` (flags must
  contain overlay-plan, not marked-magenta), log "NORD2 SNAP: 270x600 scale 4 from N layers / M slots in T ms", "SWEET HID: injected
  touch 8 down at ...". NOT verified on hardware: everything above (only host tests).
