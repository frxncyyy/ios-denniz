#!/usr/bin/env python3
"""Prototype translator patch (scratch copy only): const_loads folds every non-volatile,
non-atomic load of a field of a never-written constant global at constant indices, not only
pointer loads. Usage: patch_const_loads.py <translator root>"""
import sys
p=sys.argv[1]+'/crates/metal2vulkan-air/src/canon/const_loads.rs'
t=open(p).read()
old='''        m.tick(1)?;
        let Some(space) = parts.types.ptr_space(inst.ty) else {
            continue;
        };
        if !matches!(parts.types.get(inst.ty), Some(Type::Ptr { .. })) {
            continue;
        }
        let ptr = match inst.kind {
            InstKind::Load {
                ptr,
                volatile: false,
                atomic: None,
                ..
            } => ptr,
            // `inttoptr` of an integer load of the pointer's size: a copy through an integer.
            InstKind::Cast {
                op: CastOp::IntToPtr,
                v: Value::Inst(l),
            } => match body.insts.get(l).map(|l| (&l.kind, l.ty)) {'''
new='''        m.tick(1)?;
        let ptr = match inst.kind {
            // Any type: the field of a never-written constant global is its initializer's.
            InstKind::Load {
                ptr,
                volatile: false,
                atomic: None,
                ..
            } => ptr,
            // `inttoptr` of an integer load of the pointer's size: a copy through an integer.
            InstKind::Cast {
                op: CastOp::IntToPtr,
                v: Value::Inst(l),
            } => {
                let Some(space) = parts.types.ptr_space(inst.ty) else {
                    continue;
                };
                if !matches!(parts.types.get(inst.ty), Some(Type::Ptr { .. })) {
                    continue;
                }
                match body.insts.get(l).map(|l| (&l.kind, l.ty)) {'''
assert t.count(old)==1,'anchor'
t=t.replace(old,new)
old2='''                {
                    ptr
                }
                _ => continue,
            },
            _ => continue,
        };'''
new2='''                {
                    ptr
                }
                _ => continue,
            }
            }
            _ => continue,
        };'''
assert t.count(old2)==1,'anchor2'
t=t.replace(old2,new2)
open(p,'w').write(t)
print('patched',p)
