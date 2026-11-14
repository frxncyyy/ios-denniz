## cutout-20261114: 0550 punch hole / status bar (image q10jco, NOT device-tested)

Problem (user): the top-left front-camera hole covers the status bar clock; status bar looks misaligned; keyboard etc. look cut.

Geometry today [V-code]: iOS renders 1125x2436 (iPhone X, 375x812 pt @3x); the panel shows the centred 1080x2400 part 1:1
(SweetFramebuffer.cpp fOffX=22/fOffY=18, sweet_fb_blend.h sweet_fb_layer_clip; SweetHID.cpp touch uses the same +22/+18).
No scaling and no aspect distortion. Lost: iOS columns 0..21 / 1102..1124 (7.3/7.7 pt per side: the outer keys q/a/p/l/shift/
delete lose ~13 of ~94 px) and rows 0..17 / 2418..2435 (6 pt top/bottom: only margins; the home indicator at iOS rows ~2397..2412
is shown at panel rows ~2379..2394). A uniform fit would need scale 0.96 (1080x2338) and the OVL has no scaler -> not done.

Status bar provider [V-disasm UIKitCore 20H392]: +[_UIStatusBarVisualProvider_Split visualProviderSubclassForScreen:...]
matches _nativeDisplayBounds width + _exclusionArea size -> Split1125 (height 44 pt, notchSize 209x30, leadingCenteringOffset
-2/3, constants). Leading items centred at ~46.7 pt (iOS x ~140 = panel ~118) in the 83 pt ear. No default / MobileGestalt knob
moves them (CustomSplit/DynamicSplit exist but are selected by the same matcher / the SpringBoard "Jindo" feature flag; the
island is always centred). => no guest payload; fix at presentation level.

Hole [I]: denniz Android trees: config_mainBuiltInDisplayCutout bounding box x 0..150 (or 0..158), y 0..107 (115);
rounded_corner_radius 115 px. Estimated hole centre (77,56) r 33, +-12 px. Estimated clock box panel x 58..178, y 17..50.

0550 = "band rotation" in the overlay plan (sweet_fb_cutout.h, pure; CPU-free: extra OVL slots read sub-rectangles of the same
validated surfaces). Band = panel rows 0..113 (the 44 pt status bar). Leading: [0, 227+72) rotated right by 72: the left ear
shows at x 72..298 (clock ~130..250, clear of the hole), the empty notch-zone strip x 227..298 fills x 0..71 around the hole.
Trailing: [830, 1080) rotated so the right ear moves 24 px left (battery away from the panel corner). Pieces sit above every
layer (layer 0's opaque, upper layers' blended in order) = exact composite, moved. Needs layer 0 covering the band and free
slots (1 layer: 3 slots per pipe, 2 layers: 6; 4L off or 3 full layers -> frame shown unrotated, counted "slots").
Touch: SweetHID applies the inverse rotation in the band while the programmed frame is rotated (sweet_fb_cut_live).
Tunables: kern.n2cut 0/1, kern.n2cut_dx 72, kern.n2cut_tdx 24, kern.n2cut_band 114, kern.n2cut_ear 227, kern.n2cut_tear 854,
kern.n2cutstat. Kill switch at boot: n2cut=0 or n2gdv +512 (n2gdv=521 fits the 4 spare characters).
Host test: test/test_cutout.cpp (rotation bijection/round trip, unit steps, plan raster == unrotated raster at the inverse x for
1/2 layers incl. plane alpha, touch lands on the drawn iOS pixel, pieces read inside the layer rectangles, refusals leave the
plan untouched, clock clears the hole+tolerance); mutation-checked (4 mutants all caught). test/test_cutout_c.c = C build.
Compile: no new warnings except the 28 exempt SYSCTL-macro ones (SweetHID unchanged at 1331).
Risks: seams on photographic wallpapers in the band; landscape / full-screen video: a 299x114 and a 250x114 region at the
top corners (portrait coordinates) are rotated too (n2cut=0 if annoying); expanded (Control Center) status bar items wider than
the ear would be split; hole/clock positions are estimates -> tune kern.n2cut_dx on the phone.
