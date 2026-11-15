#!/usr/bin/env python3
"""Minimal Apple Device Tree (ADT) reader/writer used by the Nord 2 release tools (no device data inside).
Format: node = u32 nprops, u32 nchildren, props, children; prop = name[32], u32 length (bit31 = flag), value padded to 4."""
import struct


class Node:
    def __init__(self):
        self.props = []      # (name, flags, value)
        self.children = []

    @property
    def name(self):
        for k, _, v in self.props:
            if k == 'name':
                return v.split(b'\0')[0].decode(errors='replace')
        return None

    def prop(self, key):
        for k, _, v in self.props:
            if k == key:
                return v
        return None

    def set(self, key, value, flags=0):
        for i, (k, f, v) in enumerate(self.props):
            if k == key:
                self.props[i] = (k, f, value)
                return
        self.props.append((key, flags, value))

    def remove(self, key):
        n = len(self.props)
        self.props = [p for p in self.props if p[0] != key]
        return n - len(self.props)


def parse(b, o=0):
    n = Node()
    np_, nc = struct.unpack_from('<II', b, o)
    o += 8
    if np_ > 4096 or nc > 4096:
        raise ValueError('implausible ADT node')
    for _ in range(np_):
        k = b[o:o + 32].split(b'\0')[0].decode(errors='replace')
        ln = struct.unpack_from('<I', b, o + 32)[0]
        flags, ln = ln >> 31, ln & 0x7fffffff
        if o + 36 + ln > len(b):
            raise ValueError('ADT property overruns the blob')
        n.props.append((k, flags, b[o + 36:o + 36 + ln]))
        o += 36 + ((ln + 3) & ~3)
    for _ in range(nc):
        c, o = parse(b, o)
        n.children.append(c)
    return n, o


def parse_all(b):
    n, end = parse(b, 0)
    if end != len(b):
        raise ValueError('ADT trailing bytes (%d of %d used)' % (end, len(b)))
    return n


def serialize(n):
    out = bytearray(struct.pack('<II', len(n.props), len(n.children)))
    for k, f, v in n.props:
        kb = k.encode()
        if len(kb) >= 32:
            raise ValueError('property name too long: ' + k)
        out += kb + b'\0' * (32 - len(kb)) + struct.pack('<I', (f << 31) | len(v)) + v + b'\0' * (-len(v) % 4)
    for c in n.children:
        out += serialize(c)
    return bytes(out)


def child(n, name):
    cs = [c for c in n.children if c.name == name]
    if len(cs) != 1:
        raise ValueError('missing or duplicate node ' + name)
    return cs[0]


def walk(n, path=''):
    p = path + '/' + (n.name or '?')
    yield p, n
    for c in n.children:
        yield from walk(c, p)
