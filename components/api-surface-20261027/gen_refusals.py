#!/usr/bin/env python3
"""Generates Objective-C refusal definitions for protocol methods taken from the iOS 16.5 SDK headers.

Usage: gen_refusals.py HEADER SELECTOR... ; prints '- (ret)sel:(type)arg ... {...}' lines.
Parameter types are mapped to the frontend's own typedefs (TeguRange, TeguSize...)."""
import re, sys, os
G = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SDK = os.path.join(G, 'build/apple-toolchain/sdks/iPhoneOS16.5.sdk/System/Library/Frameworks/Metal.framework/Headers')
def clean(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S); t = re.sub(r'//[^\n]*', '', t); return t
def protos(text):
    for m in re.finditer(r'(?:^|\n)\s*([-+]\s*\([^;{]*?\)[^;{]*?);', text):
        d = re.sub(r'\s+', ' ', m.group(1))
        d = re.sub(r'API_\w+\((?:[^()]|\([^()]*\))*\)', '', d); d = re.sub(r'MTL_\w+', '', d)
        d = re.sub(r'NS_\w+(\([^)]*\))?', '', d)
        yield d.strip()
def sel_of(d):
    return ''.join(re.findall(r'([A-Za-z_]\w*:)', re.sub(r'\([^()]*(?:\([^()]*\)[^()]*)*\)', '()', d))) or re.sub(r'^[-+]\s*\([^)]*\)\s*', '', d).strip()
def map_type(t):
    t = re.sub(r'\bnullable\b|\bnonnull\b|__nullable|__nonnull|_Nullable|_Nonnull|__unsafe_unretained', '', t).strip()
    t = re.sub(r'id\s*<[^>]*>', 'id', t)
    t = t.replace('NSRange', 'TeguRange').replace('MTLSize', 'MTLSize')
    t = re.sub(r'MTL\w+\s*\*', 'id ', t) if re.search(r'MTL\w+\s*\*', t) and 'const' not in t else t
    t = re.sub(r'\bconst\s+id\s*\*', 'const id *', t)
    t = re.sub(r'const MTLVertexAmplificationViewMapping \*', 'const void *', t)
    return re.sub(r'\s+', ' ', t).strip()
def main():
    header, sels = sys.argv[1], set(sys.argv[2:])
    text = clean(open(os.path.join(SDK, header)).read())
    seen = set()
    for d in protos(text):
        s = sel_of(d)
        if s not in sels or s in seen: continue
        seen.add(s)
        m = re.match(r'[-+]\s*\(([^)]*)\)\s*(.*)$', d)
        ret, rest = m.group(1).strip(), m.group(2)
        parts = re.findall(r'([A-Za-z_]\w*):\s*\(((?:[^()]|\([^()]*\))*)\)\s*(\w+)', rest)
        sig = ' '.join('%s:(%s)%s' % (n, map_type(t), a) for n, t, a in parts) if parts else rest.strip()
        args = ';'.join('(void)%s' % a for _, _, a in parts)
        mode = os.environ.get('GEN_MODE', 'encoder')
        if mode == 'object':
            rt = ret if ret in ('id', 'NSUInteger', 'BOOL', 'void', 'float') else None
            if rt is None:
                sys.stderr.write('SKIP struct return %s\n' % s); continue
            body = '%s;tegu_api_unsupported("%s");%s' % (args, s.rstrip(':').split(':')[0], '' if rt == 'void' else 'return 0;')
            print('- (%s)%s { %s }' % ('signed char' if rt == 'BOOL' else rt, sig, body)); continue
        print('- (%s)%s { %s;TEGU_API_REFUSE(command,"%s"); }' % (ret if ret != 'void' else 'void', sig, args + (';' if args else '') if False else args, 'encoder_' + s.rstrip(':').split(':')[0]))
    missing = sels - seen
    if missing: sys.stderr.write('NOT FOUND: %s\n' % sorted(missing))
main()
