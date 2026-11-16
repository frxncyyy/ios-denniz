#!/usr/bin/env python3
"""Run the unchanged staged XNU interrupt walker against actual ADT properties.

Only OS containers/registry ownership are modeled. A property-read budget
detects nontermination. This is not a hardware/interrupt-delivery test.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import adt

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('adt', type=Path)
ap.add_argument('--expect-cycle', action='store_true')
args = ap.parse_args()
tree, end = adt.parse(args.adt.read_bytes())
assert end == args.adt.stat().st_size
source = ROOT / 'gpu/build/xnu-stage/iokit/Kernel/IODeviceTreeSupport.cpp'
original = source.read_text()
begin = original.index('static bool\nGetUInt32(')
finish = original.index('\nIOReturn\nIODTGetInterruptOptions(', begin)
walker = original[begin:finish]

stub = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using UInt32 = uint32_t;
using IOItemCount = unsigned;
#define LIBKERN_RETURNS_RETAINED
#define IODTSUPPORTDEBUG 0
#define OSDynamicCast(T,p) dynamic_cast<T *>(p)
#define kprintf(...) ((void)0)
struct Cycle {};
static unsigned reads;
struct OSObject { virtual ~OSObject() = default; void release() {} };
struct OSData : OSObject {
    std::vector<uint8_t> bytes;
    unsigned getLength() { return bytes.size(); }
    const void *getBytesNoCopy() { return bytes.data(); }
    static OSData *withBytesNoCopy(void *p, unsigned n);
};
struct OSSymbol : OSObject {
    std::string name;
    explicit OSSymbol(const char *s): name(s) {}
    static const OSSymbol *withCString(const char *p);
};
static std::vector<std::unique_ptr<OSObject>> objects;
OSData *OSData::withBytesNoCopy(void *p, unsigned n) {
    auto d = std::make_unique<OSData>();
    d->bytes.assign((uint8_t *)p, (uint8_t *)p+n);
    auto result = d.get(); objects.push_back(std::move(d)); return result;
}
const OSSymbol *OSSymbol::withCString(const char *s) {
    auto p = std::make_unique<OSSymbol>(s); auto result = p.get();
    objects.push_back(std::move(p)); return result;
}
struct IORegistryEntry {
    IORegistryEntry *parent = nullptr;
    std::map<std::string, OSData *> props;
    OSObject *getProperty(const char *key) {
        if (++reads > 1000) throw Cycle();
        auto p = props.find(key); return p == props.end() ? nullptr : p->second;
    }
    OSObject *getProperty(const OSSymbol *key) { return getProperty(key->name.c_str()); }
    OSObject *copyProperty(const OSSymbol *key) { return getProperty(key); }
    IORegistryEntry *getParentEntry(int) { return parent; }
};
static std::vector<std::unique_ptr<IORegistryEntry>> nodes;
static const int gIODTPlane = 0;
static const OSSymbol *gIODTPHandleKey = OSSymbol::withCString("AAPL,phandle");
static const OSSymbol *gIODTInterruptParentKey = OSSymbol::withCString("interrupt-parent");
static const OSSymbol *gIODTInterruptCellKey = OSSymbol::withCString("#interrupt-cells");
static const OSSymbol *gIODTAddressCellKey = OSSymbol::withCString("#address-cells");
static IORegistryEntry *FindPHandle(UInt32 phandle) {
    for (auto &node: nodes) {
        // Mirrors AddPHandle's eligibility test, not all phandles are indexed.
        if (!node->getProperty(gIODTInterruptCellKey)) continue;
        auto p = dynamic_cast<OSData *>(node->getProperty(gIODTPHandleKey));
        if (p && p->getLength() == 4 && !memcmp(p->getBytesNoCopy(), &phandle, 4)) return node.get();
    }
    return nullptr;
}
'''
rows = []
relevant = {'AAPL,phandle', '#interrupt-cells', '#address-cells',
            'interrupt-parent', 'interrupt-controller', 'interrupt-map',
            'interrupt-map-mask', 'reg', 'interrupts'}

def visit(node, parent=-1):
    index = len(rows)
    rows.append((node, parent))
    for child in node.children:
        visit(child, index)

visit(tree)
setup = []
target = None
for index, (node, parent) in enumerate(rows):
    setup.append('nodes.emplace_back(new IORegistryEntry);')
    if parent >= 0:
        setup.append(f'nodes[{index}]->parent = nodes[{parent}].get();')
    if node.name == 'mali':
        assert target is None
        target = index
    for name, _, value in node.props:
        if name not in relevant:
            continue
        # No untrusted strings become executable C++.
        literal = ','.join(str(b) for b in value) or '0'
        setup.append(f'{{ uint8_t v[] = {{{literal}}}; nodes[{index}]->props[{json.dumps(name)}] = OSData::withBytesNoCopy(v,{len(value)}); }}')
assert target is not None
test = r'''
    auto irq = dynamic_cast<OSData *>(nodes[TARGET]->getProperty("interrupts"));
    assert(irq && irq->getLength() == 20);
    auto bits = (UInt32 *)irq->getBytesNoCopy();
    auto parent = dynamic_cast<OSData *>(nodes[TARGET]->getProperty("interrupt-parent"));
    char expected[48] = {};
    if (parent) snprintf(expected, sizeof(expected), "IOInterruptController%08X", *(UInt32 *)parent->getBytesNoCopy());
    for (unsigned i=0; i<5; i++) {
        OSData *out = nullptr; const OSSymbol *controller = nullptr;
        auto used = IODTMapOneInterrupt(nodes[TARGET].get(), bits+i, i, &out, &controller);
        assert(used == 1 && out && out->getLength() == 4);
        assert(!memcmp(out->getBytesNoCopy(), bits+i, 4));
        assert(controller && controller->name == expected);
    }
    puts("PASS: five SPI cells resolve to the explicit GIC controller");
    return 0;
'''.replace('TARGET', str(target))
program = stub + walker + '\nint main() { try {\n' + '\n'.join(setup) + test
program += '} catch (Cycle &) { puts("CYCLE: original XNU walker exceeds 1000 property reads"); return 2; } }\n'
with tempfile.TemporaryDirectory(prefix='nord2-adt-walk-') as tmp:
    cpp = Path(tmp) / 'walk.cpp'
    binary = Path(tmp) / 'walk'
    cpp.write_text(program)
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    print(result.stdout.strip())
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    assert result.returncode == (2 if args.expect_cycle else 0), result.returncode
    assert not result.stderr, 'sanitizer diagnostic'
print('Unmodified walker SHA256:', hashlib.sha256(walker.encode()).hexdigest())
