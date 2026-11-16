#!/usr/bin/env python3
"""Host re-implementation of the pipeline cache keys (pipeline_cache_frontend.inc,
library_frontend.inc, pipeline_cache.c).  Pure hashing: no compiler is run, so a
candidate pipeline description can be checked against a device key prefix in
microseconds.  Every function mirrors its C counterpart one for one; the host
test gpu/tests/pipeline_keys_test.py cross-checks them against the C code.

Key material is little-endian: u32 fields, and every variable-length field is
prefixed by its u64 length (tegu_pc_key_bytes)."""
import hashlib
import struct

AIR_VERTEX, AIR_BLEND, AIR_ATTACHMENTS, AIR_ATTACHMENT_BLENDS = 1, 2, 3, 4


class Key:
    def __init__(self):
        self.h = hashlib.sha256()

    def u32(self, v):
        self.h.update(struct.pack('<I', v & 0xffffffff))
        return self

    def bytes(self, b):
        self.h.update(struct.pack('<Q', len(b)) + b)
        return self

    def str(self, s):
        return self.bytes(s.encode() if isinstance(s, str) else s)

    def digest(self):
        return self.h.digest()


def library_identity(source_sha256_hex):
    """tegu_pc_library_identity: source_sha256_hex is the lowercase hex SHA-256 of the metallib file."""
    return Key().str('TPC-LIB-1').str(source_sha256_hex).digest()


class Constant:
    """One TeguAIRFunctionConstant: kind 0 = indexed, 1 = named."""
    def __init__(self, type, data, name=None, index=0):
        self.kind = 1 if name is not None else 0
        self.type, self.data, self.name, self.index = type, bytes(data), name, index

    def replay(self):
        return '%u:%u:%u:%s:%s;' % (self.kind, self.type, 0 if self.kind else self.index,
                                    self.name if self.kind else '', self.data.hex())


def specialized_identity(parent, entry, constants):
    """tegu_pc_specialized_identity (constants in the order the application passed them)."""
    k = Key().str('TPC-SPEC-1').bytes(parent).bytes(entry.encode()).u32(len(constants))
    for c in constants:
        k.u32(c.kind).u32(c.type).u32(0 if c.kind else c.index)
        k.bytes(c.name.encode() if c.kind else b'').bytes(c.data)
    return k.digest()


def specialized_replay(entry, constants):
    """tegu_pc_specialized_replay: the text a TPC_MISS record carries (vspec=/fspec=)."""
    return '%s|%d|%s' % (entry, len(constants), ''.join(c.replay() for c in constants))


def parse_replay(text):
    """Inverse of specialized_replay: (entry, [Constant])."""
    entry, count, rest = text.split('|', 2)
    out = []
    for item in [x for x in rest.split(';') if x]:
        kind, type_, index, name, data = item.split(':', 4)
        out.append(Constant(int(type_), bytes.fromhex(data), name if kind == '1' else None, int(index)))
    assert len(out) == int(count), (len(out), count)
    return entry, out


def air_key(build, variant, vertex_identity, vertex_name, fragment_identity, fragment_name, fmt,
            targets, blends, attributes, layouts, profile, msgcap, plan='m2v'):
    """tegu_pc_render_create key.  targets: [(format, write_mask)]; blends: [9-tuples] (already
    the list the C code keys: 1 for AIR_BLEND, one per target for AIR_ATTACHMENT_BLENDS, else []);
    attributes: [(index, format, offset, buffer)]; layouts: [(index, stride, step_function, step_rate)]."""
    # plan='air': a generic creation planned from AIR (pipeline_cache_frontend.inc tegu_pc_plan_air)
    k = Key().str('TPC-AIRPLAN-1' if plan == 'air' else 'TPC-AIR-1').str(build).u32(variant)
    k.bytes(vertex_identity).bytes(vertex_name.encode()).bytes(fragment_identity).bytes(fragment_name.encode())
    k.u32(fmt).u32(len(targets))
    for f, m in targets:
        k.u32(f).u32(m)
    k.u32(len(blends))
    for b in blends:
        for v in b:
            k.u32(v)
    k.u32(len(attributes))
    for a in attributes:
        for v in a:
            k.u32(v)
    k.u32(len(layouts))
    for l in layouts:
        for v in l:
            k.u32(v)
    k.bytes(profile).u32(msgcap)
    return k.digest()


def mali_key(build, entry, rt, sizeof_graphics, pair_bytes, plan_bytes, msgcap, vs, fs, ctx=None):
    """tegu_pc_mali_pair_ctx key.  ctx = (vc_bytes, vcn, fc_bytes, fcn, options_bytes) for entry 11."""
    k = Key().str('TPC-MALI-1').str(build).u32(entry).u32(rt).u32(sizeof_graphics)
    k.u32(pair_bytes).u32(plan_bytes).u32(msgcap).bytes(vs).bytes(fs)
    if ctx:
        vc, vcn, fc, fcn, opt = ctx
        k.u32(vcn).bytes(vc).u32(fcn).bytes(fc).bytes(opt)
    return k.digest()
