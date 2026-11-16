# makes ship/{guest-api99a.sh, guest-api99a-undo.sh, owned-api99a.json, payload-api99a{,-off,-undo}} from texlayout1's (the
# transaction pattern the orchestrator already runs), for the Nord2Metal given: base = texlayout1 (3ae0c2f8) on gpufinal6's library.
import hashlib, json, os, shutil, sys
B, NEWFILE = sys.argv[1:3]
A = B + '/apps-20261104'; O = B + '/y2s99a-api-gaps/ship'
NEW = hashlib.sha256(open(NEWFILE, 'rb').read()).hexdigest()
TL = '3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d'
G6 = 'fe406e9c91b42f063235cbac4035977e63bf59012f7c731a0aa4587139435bfe'
os.makedirs(O, exist_ok=True)
def conv(src, dst, header):
    s = open(A + '/' + src).read()
    # the old/new states
    s = s.replace("NEW='%s'" % TL, "NEW='%s'" % NEW)
    s = s.replace("OLD={'%s': 'gpufinal6 (y2s96-final)'}" % G6, "OLD={'%s': 'texlayout1 (y2s98-texlayout)'}" % TL)
    s = s.replace("FLAGS=('no-texlayout',)", "FLAGS=('no-api99a',)")
    s = s.replace("is not gpufinal6 (the only state texlayout1 is built on)", "is not texlayout1 (the only state api99a is built on)")
    s = s.replace("payload fallback Nord2Metal is not gpufinal6", "payload fallback Nord2Metal is not texlayout1")
    s = s.replace("neither texlayout1 nor gpufinal6", "neither api99a nor texlayout1")
    s = s.replace("is not the y2s98-texlayout one", "is not the y2s99a-api-gaps one")
    s = s.replace("the y2s98-texlayout Nord2Metal", "the y2s99a-api-gaps Nord2Metal")
    s = s.replace("texlayout1", "api99a").replace("TEXLAYOUT1", "API99A")
    # (after the rename, the base's own name must read texlayout1 again)
    s = s.replace("'api99a (y2s98-texlayout)'", "'texlayout1 (y2s98-texlayout)'").replace("is not api99a (the only state", "is not texlayout1 (the only state")
    s = s.replace("is not api99a'", "is not texlayout1'").replace("neither api99a nor api99a", "neither api99a nor texlayout1")
    s = s.replace("not api99a\\'s", "not texlayout1's")
    s = s.replace("api99a (y2s98-texlayout Nord2Metal)", "api99a (y2s99a-api-gaps Nord2Metal)")
    s = s.replace("Nord2Metal y2s98-texlayout (gpufinal6 + ASTC texture layout / texture over laid-out memory; switch no-texlayout)",
                  "Nord2Metal y2s99a-api-gaps (texlayout1 + pipeline libraries, IOSurface sub-rectangles, fences, events, numFaces, family log; switch no-api99a)")
    s = s.replace("the texture layout stays OFF (nil, gpufinal6 behaviour) until it is removed", "the y2s99a features stay OFF (texlayout1 behaviour) until it is removed")
    # the comment block of the original describes texlayout1: replace it with ours
    lines = s.split('\n'); out = []; skipping = False
    for l in lines:
        if l.startswith('# ') and ('api99a' in l or skipping) and l.startswith('# ') and not out[-1].startswith('#') and not skipping:
            skipping = True; out.extend(header); continue
        if skipping:
            if l.startswith('#'): continue
            skipping = False
        out.append(l)
    open(O + '/' + dst, 'w').write('\n'.join(out))
H_INST = ["# api99a = y2s99a-api-gaps: texlayout1 (y2s98, THE PHONE) + the API-surface gaps: MTLPipelineLibrary with an empty per-GPU",
 "# cache (ActivityRingsUI, NanoTimeKit, SiriUICore, LocalAuthenticationPrivateUI, PassKitUIFoundation, PencilKit, TextInputUI;",
 "# QuartzCore/RenderBox/VectorKit left alone), IOSurface sub-rectangle textures, -newFence, encodeSignal/WaitForEvent, numFaces,",
 "# the family-query log and the empty-slot diagnosis. ONLY Nord2Metal changes; the library stays gpufinal6's d9dd236e.",
 "# Accepted current state: exactly texlayout1 (Nord2Metal 3ae0c2f8 + libredwoodair.dylib d9dd236e); its record/backup are left for",
 "# its own undo. Backup Nord2Metal.pre-api99a, record 'api99a' (restored/removed by guest-api99a-undo.sh).",
 "# Switch /var/db/nord2-metal/no-api99a = all y2s99a features off (texlayout1 behaviour): made here only if the payload dir ships it",
 "# (payload-api99a-off), marker 'made-by-api99a'; a marked one not in the payload is cleared; one that is not ours is kept and",
 "# reported. Per-feature switches (no-plib, no-subrect, no-fence, no-events, no-faces, no-famlog, no-emptyslot) are by hand only.",
 "# Every check runs before the first write; writes are backup -> Nord2Metal -> receipt."]
H_UNDO = ["# undo of api99a (y2s99a-api-gaps Nord2Metal): Nord2Metal is put back exactly as the receipt record 'api99a' says (previous =",
 "# texlayout1 3ae0c2f8), from its backup Nord2Metal.pre-api99a, or -- only if that backup is missing -- from the copy shipped in this",
 "# payload dir when it is exactly that previous file. The library is not touched. Backup and record removed; the switch no-api99a",
 "# removed if it carries the marker 'made-by-api99a', kept otherwise. texlayout1's own record/backup are untouched, so",
 "# guest-texlayout1-undo.sh works afterwards."]
conv('guest-texlayout1.sh', 'guest-api99a.sh', H_INST)
conv('guest-texlayout1-undo.sh', 'guest-api99a-undo.sh', H_UNDO)
own = json.load(open(A + '/owned-texlayout1.json'))
own = sorted(set(own) | {'/private/var/db/nord2-metal/Nord2Metal.pre-api99a', '/private/var/db/nord2-metal/no-api99a'})
json.dump(own, open(O + '/owned-api99a.json', 'w'))
for d in ('payload-api99a', 'payload-api99a-off', 'payload-api99a-undo'):
    shutil.rmtree(O + '/' + d, ignore_errors=True); os.makedirs(O + '/' + d)
for d in ('payload-api99a', 'payload-api99a-off'):
    shutil.copy2(NEWFILE, O + '/' + d + '/Nord2Metal'); os.chmod(O + '/' + d + '/Nord2Metal', 0o755)
open(O + '/payload-api99a-off/no-api99a', 'w').write('made-by-api99a\n')
shutil.copy2(B + '/y2s98-texlayout/payload/Nord2Metal.bundle/Nord2Metal', O + '/payload-api99a-undo/Nord2Metal')
assert hashlib.sha256(open(O + '/payload-api99a-undo/Nord2Metal', 'rb').read()).hexdigest() == TL
plan = {"purpose": "api99a: y2s99a-api-gaps = texlayout1 (the phone) + API-surface gaps (pipeline libraries with an empty per-GPU cache, IOSurface sub-rectangles, fences, shared-event signal/wait, numFaces, family-query log, empty-slot diagnosis). ONLY Nord2Metal changes; one transaction (backup Nord2Metal.pre-api99a, record api99a); accepts exactly texlayout1 (Nord2Metal 3ae0c2f8 + library d9dd236e, untouched).",
        "guest": "guest-api99a.sh", "old_sha256_any_of": {TL: "Nord2Metal texlayout1 (y2s98-texlayout)"},
        "required_unchanged_sha256": {"libredwoodair.dylib": "d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb"},
        "new_sha256": {"Nord2Metal": NEW}, "kernel_image": "userland only, any kernel", "owned": "owned-api99a.json"}
for d in ('payload-api99a', 'payload-api99a-off'):
    json.dump(plan, open(O + '/' + d + '/plan.json', 'w'), indent=1)
json.dump({"purpose": "undo of api99a: restores texlayout1's Nord2Metal 3ae0c2f8 (backup Nord2Metal.pre-api99a, or this copy if the backup is missing).",
           "guest": "guest-api99a-undo.sh", "fallback_sha256": {"Nord2Metal": TL}, "new_sha256": {"Nord2Metal": NEW},
           "kernel_image": "userland only, any kernel", "owned": "owned-api99a.json"}, open(O + '/payload-api99a-undo/plan.json', 'w'), indent=1)
print('ship', O, 'Nord2Metal', NEW)
