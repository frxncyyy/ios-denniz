#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""V03 oracle: compare the loader's applied relocations, symbols and init calls with llvm-readelf.

The loader itself never decides what is expected here.  For every object the oracle reads:
  - llvm-readelf -r -W      : the relocation tables (plain REL, Android packed, RELR expanded)
  - llvm-readelf --dyn-syms : symbol values / bindings
  - llvm-readelf -lW / -SW  : PT_LOAD ranges and section offsets
  - the object file bytes   : the implicit addends of REL/RELR relocations
and computes the guest address each relocation must produce, given the load bases printed by t_v03.
"""

import os
import re
import struct
import subprocess
import sys

READELF = os.environ.get("LLVM_READELF", "llvm-readelf")
T_V03 = os.environ["T_V03"]
FIX = os.environ["V03_FIXTURES"]
VENDOR = os.environ.get("N2HW_VENDOR_DIR",
                        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "../../../build/cam-vendor-20261109/vfs"))
VENDOR = os.path.abspath(VENDOR)

N2A32_HOSTCALL_BASE = 0xFFF00000
R_ARM_ABS32 = 2
R_ARM_GLOB_DAT = 21
R_ARM_JUMP_SLOT = 22
R_ARM_RELATIVE = 23
TYPE_NUM = {"R_ARM_ABS32": R_ARM_ABS32, "R_ARM_GLOB_DAT": R_ARM_GLOB_DAT,
            "R_ARM_JUMP_SLOT": R_ARM_JUMP_SLOT, "R_ARM_RELATIVE": R_ARM_RELATIVE}


class Fail(Exception):
    pass


checks = 0


def ok(cond, msg):
    global checks
    checks += 1
    if not cond:
        raise Fail(msg)


def eq(got, want, msg):
    ok(got == want, "%s: got %r, expected %r" % (msg, got, want))


def readelf(args, path):
    try:
        return subprocess.check_output([READELF] + args + [path], text=True,
                                       stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        raise Fail("llvm-readelf %s failed on %s:\n%s" % (" ".join(args), path, e.output))


def parse_dynsym(path):
    txt = readelf(["--dyn-syms", "-W"], path)
    syms = []
    for line in txt.splitlines():
        m = re.match(r"^\s*(\d+):\s+([0-9a-f]+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*(.*)$", line)
        if not m:
            continue
        name = m.group(8).strip()
        if "@" in name:
            name = name.split("@")[0]
        syms.append({"num": int(m.group(1)), "value": int(m.group(2), 16),
                     "size": int(m.group(3)), "type": m.group(4), "bind": m.group(5),
                     "vis": m.group(6), "ndx": m.group(7), "name": name})
    if not syms:
        raise Fail("no .dynsym in %s" % path)
    for i, s in enumerate(syms):
        if s["num"] != i:
            raise Fail("symbol numbering gap in %s" % path)
    return syms


def parse_phdrs(path):
    txt = readelf(["-lW"], path)
    ph = []
    for line in txt.splitlines():
        m = re.match(r"^\s*(\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+"
                     r"0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+([RWE ]+?)\s+0x([0-9a-f]+)\s*$", line)
        if not m:
            continue
        ph.append({"type": m.group(1), "off": int(m.group(2), 16),
                   "vaddr": int(m.group(3), 16), "paddr": int(m.group(4), 16),
                   "filesz": int(m.group(5), 16), "memsz": int(m.group(6), 16),
                   "flags": m.group(7), "align": int(m.group(8), 16)})
    return ph


def parse_sections(path):
    txt = readelf(["-SW"], path)
    secs = {}
    for line in txt.splitlines():
        m = re.match(r"^\s*\[\s*(\d+)\]\s+(\S*)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+"
                     r"([0-9a-f]+)\s+([0-9a-f]+)", line)
        if not m:
            continue
        name = m.group(2)
        if not name:
            continue
        secs[name] = {"idx": int(m.group(1)), "type": m.group(3),
                      "addr": int(m.group(4), 16), "off": int(m.group(5), 16),
                      "size": int(m.group(6), 16), "entsize": int(m.group(7), 16)}
    return secs


def parse_dynamic(path):
    txt = readelf(["-dW"], path)
    dyn = {}
    for line in txt.splitlines():
        m = re.match(r"^\s*0x([0-9a-f]+) \((\S+)\)\s+(.*)$", line)
        if not m:
            continue
        tag, rest = m.group(2), m.group(3).strip()
        m2 = re.search(r"\[([^\]]*)\]", rest)
        if tag == "NEEDED":
            dyn.setdefault("NEEDED", []).append(m2.group(1) if m2 else rest)
        elif tag == "SONAME":
            dyn["SONAME"] = m2.group(1) if m2 else rest
        else:
            tok = rest.split()[0] if rest else "0"
            try:
                dyn[tag] = int(tok, 0)
            except ValueError:
                dyn[tag] = rest
    return dyn


def parse_relocs(path):
    txt = readelf(["-r", "-W"], path)
    relocs = []
    counts = {}
    sec = None
    for line in txt.splitlines():
        m = re.match(r"^Relocation section '(\S+)' at offset 0x[0-9a-f]+ contains (\d+) entries", line)
        if m:
            sec = m.group(1)
            counts[sec] = int(m.group(2))
            continue
        m = re.match(r"^\s*([0-9a-f]{8})\s+([0-9a-f]{8})\s+(R_ARM_\w+)\s*(.*)$", line)
        if m and sec in (".rel.dyn", ".rel.plt"):
            info = int(m.group(2), 16)
            relocs.append({"section": sec, "offset": int(m.group(1), 16),
                           "info": info, "type": m.group(3), "symidx": info >> 8})
        elif sec is not None and sec.startswith(".rela"):
            raise Fail("RELA section in %s: the loader does not support it" % path)
    return relocs, counts


def decode_relr(words):
    addrs = []
    nxt = 0
    for w in words:
        if (w & 1) == 0:
            addrs.append(w)
            nxt = w + 4
        else:
            for bit in range(1, 32):
                if w & (1 << bit):
                    addrs.append(nxt + (bit - 1) * 4)
            nxt += 31 * 4
    return addrs


class Obj:
    def __init__(self, soname, path):
        self.soname = soname
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.syms = parse_dynsym(path)
        self.phdrs = parse_phdrs(path)
        self.sections = parse_sections(path)
        self.dyn = parse_dynamic(path)
        self.relocs, self.reloc_counts = parse_relocs(path)
        for name in (".rel.dyn", ".rel.plt"):
            if name in self.reloc_counts:
                eq(sum(1 for r in self.relocs if r["section"] == name), self.reloc_counts[name],
                   "%s: %s row count vs llvm-readelf header" % (soname, name))
        self.relr = []
        self.relr_count = 0
        self.base = 0
        self.span = 0
        self.map_lo = 0
        if ".relr.dyn" in self.sections:
            sec = self.sections[".relr.dyn"]
            raw = self.data[sec["off"]:sec["off"] + sec["size"]]
            ok(len(raw) == sec["size"], "%s: short RELR read" % path)
            words = list(struct.unpack("<%dI" % (len(raw) // 4), raw))
            self.relr = decode_relr(words)
            self.relr_count = self.reloc_counts.get(".relr.dyn", 0)
            eq(len(self.relr), self.relr_count, "%s: RELR entry count vs llvm-readelf" % path)
        self.export_map = {}
        for s in self.syms:
            if is_export(s) and s["name"] and s["name"] not in self.export_map:
                self.export_map[s["name"]] = s
        self.import_names = set()
        for s in self.syms:
            if s["ndx"] == "UND" and s["name"]:
                self.import_names.add(s["name"])
        # Span implied by the program headers, exactly as n2elf_load computes it.
        lo = None
        hi = None
        for p in self.phdrs:
            if p["type"] != "LOAD":
                continue
            a = p["vaddr"] & ~0xFFF
            b = (p["vaddr"] + p["memsz"] + 0xFFF) & ~0xFFF
            lo = a if lo is None else min(lo, a)
            hi = b if hi is None else max(hi, b)
        self.map_lo = lo
        self.span = hi - lo

    def u32_at_vaddr(self, vaddr):
        for p in self.phdrs:
            if p["type"] != "LOAD":
                continue
            if p["vaddr"] <= vaddr < p["vaddr"] + p["filesz"]:
                off = p["off"] + (vaddr - p["vaddr"])
                return struct.unpack_from("<I", self.data, off)[0]
            if p["vaddr"] <= vaddr < p["vaddr"] + p["memsz"]:
                return 0
        raise Fail("%s: vaddr 0x%x is not in any PT_LOAD" % (self.path, vaddr))


def is_export(s):
    return (s["ndx"] != "UND" and s["bind"] in ("GLOBAL", "WEAK", "UNIQUE") and
            s["vis"] in ("DEFAULT", "PROTECTED") and
            s["type"] not in ("SECTION", "FILE", "TLS"))


def symbol_addr(obj, sym, resolve_name):
    if sym["ndx"] == "UND":
        if not sym["name"]:
            return 0
        return resolve_name(sym["name"])
    if sym["ndx"] == "ABS":
        return sym["value"]
    return obj.base + sym["value"]


def parse_output(text):
    out = {"images": [], "relocs": [], "shims": {}, "data": {}, "hostcalls": {},
           "unresolved": None, "syms": {}, "describes": {}, "init": [],
           "init_ret": None, "errors": [], "end": False, "mems": {}, "perms": {},
           "relink": None, "retry": None}
    for line in text.splitlines():
        f = line.split()
        if not f:
            continue
        if f[0] == "IMAGE" and len(f) == 6:
            out["images"].append({"soname": f[1], "base": int(f[2], 16),
                                  "span": int(f[3], 16), "map_lo": int(f[4], 16),
                                  "entry": int(f[5], 16)})
        elif f[0] == "RELOC" and len(f) == 4:
            out["relocs"].append((int(f[1], 16), int(f[2]), int(f[3], 16)))
        elif f[0] == "SHIM" and len(f) == 3:
            out["shims"][f[1]] = int(f[2], 16)
        elif f[0] == "DATA" and len(f) == 4:
            out["data"][f[1]] = (int(f[2], 16), int(f[3], 16))
        elif f[0] == "HOSTCALL" and len(f) == 4:
            out["hostcalls"][f[2]] = int(f[1], 16)
            out.setdefault("hostcall_null", {})[f[2]] = int(f[3])
        elif f[0] == "UNRESOLVED" and len(f) == 2:
            out["unresolved"] = int(f[1])
        elif f[0] == "SYM" and len(f) == 3:
            out["syms"][f[1]] = int(f[2], 16)
        elif f[0] == "DESCRIBE" and len(f) >= 3:
            out["describes"][int(f[1], 16)] = " ".join(f[2:])
        elif f[0] == "INIT_CALL" and len(f) == 2:
            out["init"].append(int(f[1], 16))
        elif f[0] == "MEM" and len(f) == 4:
            out["mems"][int(f[1], 16)] = bytes.fromhex(f[3])
        elif f[0] == "PERM" and len(f) == 3:
            out["perms"][int(f[1], 16)] = int(f[2])
        elif f[0] == "RELINK" and len(f) == 3:
            out["relink"] = (int(f[1]), int(f[2]))
        elif f[0] == "RETRY":
            out["retry"] = (int(f[1]), line[len("RETRY "):].split(" ", 1)[1]
                            if len(f) > 2 else "")
        elif f[0] == "INIT_RET" and len(f) == 2:
            out["init_ret"] = int(f[1])
        elif f[0] == "ERROR":
            out["errors"].append(line[len("ERROR "):])
        elif f[0] == "END":
            out["end"] = True
    return out


def run_manifest(text):
    path = os.path.join(FIX, "manifest.txt")
    with open(path, "w") as f:
        f.write(text)
    proc = subprocess.run([T_V03, "run", path], capture_output=True, text=True)
    if proc.returncode != 0:
        raise Fail("t_v03 exited %d\nstdout:\n%s\nstderr:\n%s" %
                   (proc.returncode, proc.stdout, proc.stderr))
    if proc.stderr.strip():
        raise Fail("t_v03 wrote to stderr (sanitizer?):\n%s" % proc.stderr)
    out = parse_output(proc.stdout)
    ok(out["end"], "t_v03 output has no END marker")
    return out


def manifest_for(objs, shims=(), datas=(), syms=(), describes=(), mems=(), perms=(),
                 relink=False, run_init=False, retry=False):
    lines = []
    for o in objs:
        lines.append("obj %s %s" % (o.soname, o.path))
    for s in shims:
        lines.append("shim %s" % s)
    for d in datas:
        lines.append("data %s %d %s" % (d["name"], d["size"], d["hex"]))
    for s in syms:
        lines.append("sym %s" % s)
    for a in describes:
        lines.append("describe %08x" % a)
    for a, n in mems:
        lines.append("mem %08x %d" % (a, n))
    for a in perms:
        lines.append("perm %08x" % a)
    if relink:
        lines.append("relink")
    if retry:
        lines.append("retry")
    if run_init:
        lines.append("run-init")
    return "\n".join(lines) + "\n"


def resolve_factory(objs, out, shim_names):
    def resolve(name):
        for o in objs:
            s = o.export_map.get(name)
            if s is not None:
                return s["value"] if s["ndx"] == "ABS" else o.base + s["value"]
        if name in out["shims"]:
            return out["shims"][name]
        if name in out["data"]:
            return out["data"][name][0]
        if name in out["hostcalls"]:
            return out["hostcalls"][name]
        raise Fail("no resolution for symbol %r" % name)
    return resolve


def expected_relocs(obj, resolve):
    exp = {}
    for r in obj.relocs:
        addr = obj.base + r["offset"]
        if addr in exp:
            raise Fail("%s: duplicate relocation target 0x%x" % (obj.soname, addr))
        t = r["type"]
        if t == "R_ARM_RELATIVE":
            exp[addr] = (R_ARM_RELATIVE, (obj.base + obj.u32_at_vaddr(r["offset"])) & 0xFFFFFFFF)
        elif t in ("R_ARM_GLOB_DAT", "R_ARM_JUMP_SLOT"):
            sym = obj.syms[r["symidx"]]
            exp[addr] = (TYPE_NUM[t], symbol_addr(obj, sym, resolve) & 0xFFFFFFFF)
        elif t == "R_ARM_ABS32":
            sym = obj.syms[r["symidx"]]
            addend = obj.u32_at_vaddr(r["offset"])
            exp[addr] = (R_ARM_ABS32, (symbol_addr(obj, sym, resolve) + addend) & 0xFFFFFFFF)
        else:
            raise Fail("%s: oracle does not implement %s" % (obj.soname, t))
    for off in obj.relr:
        addr = obj.base + off
        if addr in exp:
            raise Fail("%s: RELR target 0x%x also in a REL table" % (obj.soname, addr))
        exp[addr] = (R_ARM_RELATIVE, (obj.base + obj.u32_at_vaddr(off)) & 0xFFFFFFFF)
    return exp


def check_images(objs, out, e_entries):
    eq(len(out["images"]), len(objs), "number of loaded images")
    for i, (o, im) in enumerate(zip(objs, out["images"])):
        eq(im["soname"], o.soname, "image %d soname" % i)
        ok(im["base"] != 0 and (im["base"] & 0xFFF) == 0,
           "%s: load base 0x%x is not page aligned" % (o.soname, im["base"]))
        eq(im["span"], o.span, "%s: mapped span" % o.soname)
        eq(im["map_lo"], o.map_lo, "%s: mapped low address" % o.soname)
        eq(im["entry"], (im["base"] + e_entries[i]) & 0xFFFFFFFF if e_entries[i] else 0,
           "%s: entry point" % o.soname)
        o.base = im["base"]
        ok(im["base"] + im["span"] <= 0x100000000, "%s: image crosses 4 GiB" % o.soname)


def check_relocs(objs, out, resolve):
    actual = {}
    for addr, typ, val in out["relocs"]:
        if addr in actual:
            raise Fail("loader reported two relocations at 0x%x" % addr)
        actual[addr] = (typ, val)
    expected = {}
    for o in objs:
        e = expected_relocs(o, resolve)
        for a, v in e.items():
            if a in expected:
                raise Fail("oracle: two objects relocate 0x%x" % a)
            expected[a] = v
    eq(set(actual), set(expected), "relocation target address set")
    # Per-object counts, so a table silently skipped for one object is caught.
    for o in objs:
        n_exp = sum(1 for r in o.relocs) + len(o.relr)
        n_act = sum(1 for a in actual if o.base <= a < o.base + o.span)
        eq(n_act, n_exp, "%s: number of applied relocations" % o.soname)
    for a in sorted(expected):
        eq(actual[a], expected[a], "relocation at 0x%08x" % a)
    return actual


def check_unresolved(objs, out, shim_names):
    referenced = set()
    for o in objs:
        for r in o.relocs:
            sym = o.syms[r["symidx"]]
            if sym["ndx"] != "UND" or not sym["name"]:
                continue
            name = sym["name"]
            defined = any(name in x.export_map for x in objs)
            if not defined and name not in shim_names and name not in out["data"]:
                referenced.add(name)
    slots = {n: a for n, a in out["hostcalls"].items() if n not in shim_names}
    eq(set(slots), referenced, "set of unresolved imports")
    eq(out["unresolved"], len(referenced), "unresolved import count")
    for name, a in slots.items():
        ok(a >= N2A32_HOSTCALL_BASE + 4 and (a - N2A32_HOSTCALL_BASE) % 4 == 0,
           "unresolved slot for %s is not a host-call slot: 0x%x" % (name, a))
        eq(out.get("hostcall_null", {}).get(name), 1,
           "unresolved slot for %s has a NULL implementation (NOIMPORT)" % name)
    eq(len(set(slots.values())), len(slots), "unresolved slots are unique per name")
    return referenced


def check_syms(names, objs, out):
    """n2elf_sym() searches definitions, then function shims, then data shims; unresolved imports are not
    definitions and must return 0."""
    for name in names:
        want = None
        for o in objs:
            sym = o.export_map.get(name)
            if sym is not None:
                want = sym["value"] if sym["ndx"] == "ABS" else o.base + sym["value"]
                break
        if want is None and name in out["shims"]:
            want = out["shims"][name]
        if want is None and name in out["data"]:
            want = out["data"][name][0]
        if want is None:
            want = 0
        eq(out["syms"].get(name), want & 0xFFFFFFFF, "n2elf_sym(%s)" % name)


def dep_order(objs):
    by_name = {o.soname: o for o in objs}
    state = {}
    order = []

    def visit(o):
        st = state.get(o.soname, 0)
        if st:
            return
        state[o.soname] = 1
        for need in o.dyn.get("NEEDED", []):
            dep = by_name.get(need)
            if dep is not None:
                visit(dep)
        state[o.soname] = 2
        order.append(o)

    for o in objs:
        visit(o)
    return order


def check_init(objs, out, actual_relocs):
    expected = []
    for o in dep_order(objs):
        for addr_tag, size_tag in (("PREINIT_ARRAY", "PREINIT_ARRAYSZ"),):
            if addr_tag not in o.dyn or size_tag not in o.dyn:
                continue
            addr = int(o.dyn[addr_tag])
            size = int(o.dyn[size_tag])
            for off in range(0, size, 4):
                word = o.u32_at_vaddr(addr + off)
                if word in (0, 1, 0xFFFFFFFF):
                    continue
                target = (o.base + word) & 0xFFFFFFFF
                eq(actual_relocs.get(o.base + addr + off),
                   (R_ARM_RELATIVE, target), "%s: preinit array slot 0x%x" %
                   (o.soname, addr + off))
                expected.append(target)
        if "INIT" in o.dyn:
            expected.append((o.base + int(o.dyn["INIT"])) & 0xFFFFFFFF)
        for addr_tag, size_tag in (("INIT_ARRAY", "INIT_ARRAYSZ"),):
            if addr_tag not in o.dyn or size_tag not in o.dyn:
                continue
            addr = int(o.dyn[addr_tag])
            size = int(o.dyn[size_tag])
            for off in range(0, size, 4):
                word = o.u32_at_vaddr(addr + off)
                if word in (0, 1, 0xFFFFFFFF):
                    continue
                target = (o.base + word) & 0xFFFFFFFF
                eq(actual_relocs.get(o.base + addr + off),
                   (R_ARM_RELATIVE, target), "%s: init array slot 0x%x" %
                   (o.soname, addr + off))
                expected.append(target)
    eq(out["init_ret"], 0, "n2elf_run_init return code")
    eq(out["init"], expected, "init call order (dependencies first, preinit/init/init_array)")


def expected_describe(obj, addr):
    a = addr & ~1
    off = a - obj.base
    best = None
    for s in obj.syms:
        if s["ndx"] == "UND" or s["type"] in ("SECTION", "FILE") or not s["name"]:
            continue
        if s["value"] <= off and (best is None or s["value"] > best["value"]):
            best = s
    if best is not None:
        return "%s:%s+0x%x" % (obj.soname, best["name"], off - best["value"])
    return "%s+0x%x" % (obj.soname, off)


def bytes_at_vaddr(obj, vaddr, length):
    """The object bytes the loader should have copied, 0 for the bss part of a PT_LOAD."""
    out = bytearray()
    for k in range(length):
        va = vaddr + k
        got = None
        for p in obj.phdrs:
            if p["type"] != "LOAD":
                continue
            if p["vaddr"] <= va < p["vaddr"] + p["filesz"]:
                got = obj.data[p["off"] + (va - p["vaddr"])]
                break
            if p["vaddr"] <= va < p["vaddr"] + p["memsz"]:
                got = 0
                break
        if got is None:
            raise Fail("%s: 0x%x is in no PT_LOAD" % (obj.soname, va))
        out.append(got)
    return bytes(out)


def page_writable(obj, addr):
    page = (addr - obj.base) & ~0xFFF
    for p in obj.phdrs:
        if p["type"] != "LOAD" or "W" not in p["flags"]:
            continue
        a = p["vaddr"] & ~0xFFF
        b = (p["vaddr"] + p["memsz"] + 0xFFF) & ~0xFFF
        if a <= page < b:
            return 1
    return 0


def check_describes(cases, objs, out):
    for addr in cases:
        obj = None
        for o in objs:
            if o.base <= addr < o.base + o.span:
                obj = o
                break
        if obj is None:
            eq(out["describes"].get(addr), "0x%08x" % addr,
               "n2elf_describe(0x%x) outside every image" % addr)
            continue
        want = expected_describe(obj, addr)
        eq(out["describes"].get(addr), want, "n2elf_describe(0x%x)" % addr)


def check_error(out, stage, needle):
    ok(out["errors"], "expected an ERROR, got none")
    line = out["errors"][0]
    ok(line.startswith(stage + " "), "expected an error from %s, got %r" % (stage, line))
    ok(needle in line, "error %r does not mention %r" % (line, needle))


def case_synthetic():
    liba = Obj("liba.so", os.path.join(FIX, "liba.so"))
    libb = Obj("libb.so", os.path.join(FIX, "libb.so"))
    libc = Obj("libc.so", os.path.join(FIX, "libc.so"))
    libd = Obj("libd.so", os.path.join(FIX, "libd.so"))
    libarm = Obj("libarm.so", os.path.join(FIX, "libarm.so"))
    objs = [libd, libc, libb, libarm, liba]        # reversed dependency order on purpose
    shims = ["shim_fn_a"]
    datas = [{"name": "shim_data_x", "size": 4, "hex": "78563412"}]
    names = []
    seen = set()
    for o in objs:
        for s in sorted(o.syms, key=lambda x: x["num"]):
            if is_export(s) and s["name"] and s["name"] not in seen:
                names.append(s["name"])
                seen.add(s["name"])
    names += ["shim_fn_a", "shim_data_x", "never_defined_fn", "weak_missing_fn",
              "weak_fn", "no_such_symbol_zzz"]
    entries = [struct.unpack_from("<I", o.data, 24)[0] for o in objs]
    out = run_manifest(manifest_for(objs, shims, datas, names, run_init=True))
    ok(not out["errors"], "synthetic load/link failed: %r" % out["errors"])
    check_images(objs, out, entries)
    resolve = resolve_factory(objs, out, set(shims))
    relocs = check_relocs(objs, out, resolve)
    eq({t for _, t, _ in out["relocs"]}, {R_ARM_ABS32, R_ARM_GLOB_DAT, R_ARM_JUMP_SLOT,
                                          R_ARM_RELATIVE}, "synthetic relocation type coverage")
    unresolved = check_unresolved(objs, out, set(shims))
    ok("weak_missing_fn" in unresolved, "weak undefined import is bound to the unresolved slot")
    eq(out["syms"].get("weak_fn"), liba.base + liba.export_map["weak_fn"]["value"],
       "weak defined symbol lookup")
    eq(out["syms"].get("no_such_symbol_zzz"), 0, "lookup of an unknown symbol returns 0")
    check_syms(names, objs, out)
    check_init(objs, out, relocs)
    for d in datas:
        eq(out["data"][d["name"]][0] != 0, True, "data shim %s has an address" % d["name"])
        want = int.from_bytes((bytes.fromhex(d["hex"]) + b"\0\0\0\0")[:4], "little")
        eq(out["data"][d["name"]][1], want, "data shim %s initial bytes" % d["name"])
    # Second run: describes, guest memory/bss, page protections, link idempotence.
    addrs = [liba.base + liba.export_map["liba_fn"]["value"] + 4,
             libb.base + (libb.export_map["libb_fn"]["value"] & ~1) + 2,
             libc.base + libc.export_map["libc_data"]["value"],
             out["hostcalls"]["never_defined_fn"], 0x12345678, libd.base]
    mems = [(liba.base + liba.export_map["liba_data"]["value"], 4),
            (liba.base + liba.export_map["liba_bss"]["value"], 16)]
    perms = [liba.base + liba.export_map["liba_fn"]["value"],
             liba.base + liba.export_map["liba_data"]["value"]]
    out2 = run_manifest(manifest_for(objs, shims, datas, names, addrs, mems, perms,
                                     relink=True, run_init=True))
    ok(not out2["errors"], "synthetic second run failed: %r" % out2["errors"])
    check_images(objs, out2, entries)
    check_relocs(objs, out2, resolve_factory(objs, out2, set(shims)))
    check_unresolved(objs, out2, set(shims))
    check_describes(addrs[:3] + addrs[4:], objs, out2)
    eq(out2["describes"].get(addrs[3]), "hostcall:never_defined_fn",
       "n2elf_describe on an unresolved host-call slot")
    eq(out2["hostcalls"].get("never_defined_fn"), addrs[3],
       "unresolved slot address is deterministic across runs")
    eq(out2["mems"][mems[0][0]], bytes_at_vaddr(liba, liba.export_map["liba_data"]["value"], 4),
       "guest memory of a .data symbol")
    eq(out2["mems"][mems[1][0]], b"\0" * 16, "guest memory of a .bss symbol is zero")
    eq(out2["perms"][perms[0]], 0, "text page is read-only after relocation")
    eq(out2["perms"][perms[1]], 1, "data page is writable after relocation")
    eq(out2["relink"], (0, len(unresolved)), "second n2elf_link call is a no-op")
    # DT_INIT: lld does not emit it for these objects, so patch libd's first DT_NULL into DT_INIT=libd_fn.
    libd_fn = libd.export_map["libd_fn"]["value"]
    patch = os.path.join(FIX, "libd-init.so")
    data = bytearray(open(libd.path, "rb").read())
    dynph = next(p for p in libd.phdrs if p["type"] == "DYNAMIC")
    for i in range(dynph["filesz"] // 8):
        off = dynph["off"] + 8 * i
        tag = struct.unpack_from("<i", data, off)[0]
        if tag == 0:
            struct.pack_into("<II", data, off, 12, libd_fn)   # DT_INIT
            break
    else:
        raise Fail("libd has no DT_NULL slot to patch into DT_INIT")
    open(patch, "wb").write(data)
    libd_init = Obj("libd-init.so", patch)
    eq(int(libd_init.dyn["INIT"]), libd_fn, "patched DT_INIT value")
    out3 = run_manifest(manifest_for([libd_init], (), (), (), (), (), (), False, True))
    ok(not out3["errors"], "DT_INIT load/run failed: %r" % out3["errors"])
    check_images([libd_init], out3, [0])
    rel3 = check_relocs([libd_init], out3, resolve_factory([libd_init], out3, set()))
    check_init([libd_init], out3, rel3)
    eq(len(out3["init"]), 2, "DT_INIT and INIT_ARRAY both ran")
    return len(out["relocs"])


def case_vendor():
    files = [("vpud", "bin/vpud"), ("libvpud_vcodec.so", "lib/libvpud_vcodec.so"),
             ("libvcodecdrv.so", "lib/libvcodecdrv.so"),
             ("libvcodec_utility.so", "lib/libvcodec_utility.so"),
             ("libvcodec_oal.so", "lib/libvcodec_oal.so")]
    objs = [Obj(n, os.path.join(VENDOR, p)) for n, p in files]
    shims = []
    datas = [{"name": "__stack_chk_guard", "size": 4, "hex": "efbeadde"},
             {"name": "stderr", "size": 4, "hex": "00000000"}]
    names = []
    seen = set()
    for o in objs:
        for s in sorted(o.syms, key=lambda x: x["num"]):
            if is_export(s) and s["name"] and s["name"] not in seen and \
                    re.match(r"^[\x21-\x7e]+$", s["name"]):
                names.append(s["name"])
                seen.add(s["name"])
    entries = [struct.unpack_from("<I", o.data, 24)[0] for o in objs]
    out = run_manifest(manifest_for(objs, shims, datas, names, [], run_init=True))
    ok(not out["errors"], "vendor load/link failed: %r" % out["errors"])
    check_images(objs, out, entries)
    resolve = resolve_factory(objs, out, set(shims))
    relocs = check_relocs(objs, out, resolve)
    unresolved = check_unresolved(objs, out, set(shims))
    check_syms(names, objs, out)
    check_init(objs, out, relocs)
    for d in datas:
        want = int.from_bytes((bytes.fromhex(d["hex"]) + b"\0\0\0\0")[:4], "little")
        eq(out["data"][d["name"]][1], want, "data shim %s initial bytes" % d["name"])
    # Second run: page protections and segment contents on the real objects, plus link idempotence.
    text_obj = objs[2]
    text_sym = text_obj.export_map["H264_EncodeFrame"]
    data_sym = next(s for s in text_obj.syms if is_export(s) and s["type"] == "OBJECT")
    mems = [(text_obj.base + text_sym["value"], 4),
            (text_obj.base + data_sym["value"], 4)]
    perms = [text_obj.base + text_sym["value"], text_obj.base + data_sym["value"]]
    out2 = run_manifest(manifest_for(objs, shims, datas, [], [], mems, perms, relink=True))
    ok(not out2["errors"], "vendor second run failed: %r" % out2["errors"])
    check_images(objs, out2, entries)
    check_relocs(objs, out2, resolve_factory(objs, out2, set(shims)))
    for (addr, _), obj, sym in [(mems[0], text_obj, text_sym), (mems[1], text_obj, data_sym)]:
        eq(out2["mems"][addr], bytes_at_vaddr(obj, sym["value"], 4),
           "segment contents at %s" % sym["name"])
    eq(out2["perms"][perms[0]], 0, "vendor text page is read-only after relocation")
    eq(out2["perms"][perms[1]], 1, "vendor data page is writable after relocation")
    eq(out2["relink"], (0, len(unresolved)), "second n2elf_link call is a no-op")
    # Third run: provide a function shim for every referenced import; unresolved must become 0 and every
    # GOT/PLT value must move to the printed shim slot address.
    shim_names = sorted(unresolved)
    out3 = run_manifest(manifest_for(objs, shim_names, datas))
    ok(not out3["errors"], "vendor all-shims run failed: %r" % out3["errors"])
    check_images(objs, out3, entries)
    check_relocs(objs, out3, resolve_factory(objs, out3, set(shim_names)))
    check_unresolved(objs, out3, set(shim_names))
    eq(out3["unresolved"], 0, "unresolved imports with a full function shim table")
    eq(len(out3["shims"]), len(shim_names), "one printed shim address per import")
    eq(len(set(out3["shims"].values())), len(shim_names), "distinct host-call slots per shim")
    for name, a in out3["shims"].items():
        ok(a >= N2A32_HOSTCALL_BASE + 4 and (a - N2A32_HOSTCALL_BASE) % 4 == 0,
           "shim %s is not in the host-call page: 0x%x" % (name, a))
    # Every unique import of the five objects, for the acceptance statistic.
    undefined = set()
    defined = set()
    for o in objs:
        undefined |= o.import_names
        defined |= set(o.export_map)
    unique_imports = len(undefined - defined)
    ok(len(unresolved) <= 149, "unresolved imports %d > 149" % len(unresolved))
    per = []
    for o in objs:
        n_exp = sum(1 for r in o.relocs) + len(o.relr)
        n_act = sum(1 for a, _, _ in out["relocs"] if o.base <= a < o.base + o.span)
        per.append("%s %d/%d" % (o.soname, n_act, n_exp))
        eq(n_act, n_exp, "%s: applied/readelf relocation count" % o.soname)
    print("V03 vendor per object (applied/readelf): " + ", ".join(per))
    return len(relocs), len(unresolved), unique_imports, len(out["images"])


def patch_dynamic_tag(src, dst, tag, new_value):
    """Replace the value of DT_<tag> in the first PT_DYNAMIC entry that carries it."""
    obj = Obj("patch-src", src)
    data = bytearray(open(src, "rb").read())
    dynph = next(p for p in obj.phdrs if p["type"] == "DYNAMIC")
    for i in range(dynph["filesz"] // 8):
        off = dynph["off"] + 8 * i
        cur = struct.unpack_from("<i", data, off)[0]
        if cur == tag:
            struct.pack_into("<II", data, off, tag, new_value)
            open(dst, "wb").write(data)
            return
    raise Fail("%s: no dynamic tag %d to patch" % (src, tag))


class Bare:
    """Just enough of an image description for a negative manifest (the loader must fail before linking)."""

    def __init__(self, soname, path):
        self.soname = soname
        self.path = path


def case_soname():
    """soname == NULL must use DT_SONAME, and the renamed file must still describe as libd.so."""
    src = os.path.join(FIX, "libd.so")
    dst = os.path.join(FIX, "renamed-libd.so")
    with open(dst, "wb") as f:
        f.write(open(src, "rb").read())
    obj = Obj("libd.so", dst)
    out = run_manifest("obj (null) %s\n" % dst)
    ok(not out["errors"], "DT_SONAME load failed: %r" % out["errors"])
    check_images([obj], out, [0])
    check_relocs([obj], out, resolve_factory([obj], out, set()))
    eq(obj.export_map.get("libd_fn") is not None, True, "DT_SONAME fixture still exports libd_fn")


def case_errors():
    liba = Obj("liba.so", os.path.join(FIX, "liba.so"))
    libb = Obj("libb.so", os.path.join(FIX, "libb.so"))
    # 1) ET_EXEC
    out = run_manifest(manifest_for([Bare("exec.elf", os.path.join(FIX, "exec.elf"))]))
    check_error(out, "load", "not ET_DYN")
    # 2) unsupported relocation type, named in the error
    badtype = os.path.join(FIX, "badtype.so")
    data = bytearray(open(os.path.join(FIX, "liba.so"), "rb").read())
    sec = liba.sections[".rel.dyn"]
    data[sec["off"] + 4] = 42          # r_info low byte of the first entry
    open(badtype, "wb").write(data)
    out = run_manifest(manifest_for([Bare("badtype.so", badtype)]))
    check_error(out, "link", "R_ARM_PREL31 (42)")
    # 3) broken Android packed magic
    badaps = os.path.join(FIX, "badaps2.so")
    data = bytearray(open(os.path.join(FIX, "libb.so"), "rb").read())
    sec = libb.sections[".rel.dyn"]
    eq(bytes(data[sec["off"]:sec["off"] + 4]), b"APS2", "libb packed magic before patch")
    data[sec["off"]:sec["off"] + 4] = b"APS3"
    open(badaps, "wb").write(data)
    out = run_manifest(manifest_for([Bare("badaps2.so", badaps)]))
    check_error(out, "load", "APS2")
    # 4) unresolved data import
    out = run_manifest(manifest_for([Bare("libbad.so", os.path.join(FIX, "libbad.so"))]))
    check_error(out, "link", "unresolved data import missing_data")
    # 5) truncated file
    trunc = os.path.join(FIX, "truncated.so")
    with open(trunc, "wb") as f:
        f.write(open(os.path.join(FIX, "liba.so"), "rb").read()[:40])
    out = run_manifest(manifest_for([Bare("truncated.so", trunc)]))
    check_error(out, "load", "shorter than an ELF header")
    # 6) missing file
    out = run_manifest(manifest_for([Bare("gone.so", os.path.join(FIX, "does-not-exist.so"))]))
    check_error(out, "load", "cannot open")
    # 7) DT_FINI_ARRAY pointing outside the image (parsed and validated even though no fini API exists)
    badfini = os.path.join(FIX, "badfini.so")
    patch_dynamic_tag(os.path.join(FIX, "liba.so"), badfini, 26, 0xFFFFF000)
    out = run_manifest(manifest_for([Bare("badfini.so", badfini)]))
    check_error(out, "load", "DT_FINI_ARRAY")
    # 8) malformed GNU hash header (bloom_shift >= 32)
    badhash = os.path.join(FIX, "badhash.so")
    data = bytearray(open(os.path.join(FIX, "liba.so"), "rb").read())
    gnu = liba.sections[".gnu.hash"]
    struct.pack_into("<I", data, gnu["off"] + 12, 40)
    open(badhash, "wb").write(data)
    out = run_manifest(manifest_for(
        [Bare("liba.so", badhash), Bare("libb.so", os.path.join(FIX, "libb.so"))],
        ["shim_fn_a"], [{"name": "shim_data_x", "size": 4, "hex": "00000000"}]))
    check_error(out, "link", "bloom shift")
    # 9) a link that fails halfway must refuse a retry (R1 finding F6): first entry applies, second is type 42
    badtype2 = os.path.join(FIX, "badtype2.so")
    data = bytearray(open(os.path.join(FIX, "liba.so"), "rb").read())
    sec = liba.sections[".rel.dyn"]
    data[sec["off"] + 8 + 4] = 42      # r_info low byte of the second entry
    open(badtype2, "wb").write(data)
    out = run_manifest(manifest_for([Bare("liba.so", badtype2)], retry=True))
    check_error(out, "link", "R_ARM_PREL31 (42)")
    ok(out["retry"] is not None, "retry after a failed link produced no RETRY line")
    eq(out["retry"][0], -1, "retry after a partially applied link is refused")
    ok("previous link attempt failed" in out["retry"][1],
       "retry error names the partial-link state: %r" % (out["retry"][1],))
    # 10) unsupported RELR entry size in the loader's dynamic pass
    badrelr = os.path.join(FIX, "badrelr.so")
    patch_dynamic_tag(os.path.join(FIX, "libd.so"), badrelr, 37, 8)
    out = run_manifest(manifest_for([Bare("badrelr.so", badrelr)]))
    check_error(out, "load", "RELR entry size")


def main():
    if not os.path.isdir(VENDOR):
        raise Fail("vendor directory not found: %s (set N2HW_VENDOR_DIR)" % VENDOR)
    for rel in ("bin/vpud", "lib/libvpud_vcodec.so", "lib/libvcodecdrv.so",
                "lib/libvcodec_utility.so", "lib/libvcodec_oal.so"):
        if not os.path.isfile(os.path.join(VENDOR, rel)):
            raise Fail("missing vendor object: %s" % os.path.join(VENDOR, rel))
    n_synth = case_synthetic()
    n_vendor, n_unres, n_imports, n_objs = case_vendor()
    case_soname()
    case_errors()
    print("V03 oracle: %d checks" % checks)
    print("V03 vendor: objects %d, relocations %d, unresolved %d, unique imports %d" %
          (n_objs, n_vendor, n_unres, n_imports))
    print("V03 synthetic: relocations %d" % n_synth)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fail as e:
        print("V03 ORACLE FAIL: %s" % e)
        sys.exit(1)
