#!/usr/bin/env python3
"""Executable specification ONLY; does not exercise APFS or kernel vnode APIs.

Model shows why fallback55 cannot recover a crash in a two-rename transaction.
An actual installer must implement ownership/receipt validation and the unconditional
pre-launchd recovery hook before this specification can establish useful coverage.
"""
from dataclasses import dataclass
from copy import deepcopy
@dataclass
class State:
    active: str|None='apple'
    backup: str|None=None
    stage: str|None=None
    virtual: str='disabled'
    original: str|None=None
    receipt: str|None=None

def steps(s):
    # Exclusive ownership established before any persistent layout mutation.
    assert s.active=='apple' and s.backup is None and s.stage is None and s.receipt is None
    s.original=s.virtual; s.receipt='prepared'; yield 'receipt synced'
    s.stage='verified-redwood'; yield 'stage readback and parent synced'
    s.backup=s.active; s.active=None; yield 'original renamed and parent synced'
    s.active=s.stage; s.stage=None; yield 'stage renamed and parent synced'
    s.virtual='live'; yield 'VirtualAudio restored and both parents synced'
    s.receipt='committed'; yield 'commit readback and parent synced'

def rollback(s):
    assert s.receipt in ('prepared','committed')
    s.virtual=s.original
    if s.backup:
        assert s.backup=='apple' and s.active in (None,'verified-redwood')
        s.active=s.backup; s.backup=None
    assert s.active=='apple'
    s.stage=None; s.receipt=None; s.original=None

def boot_repair(s):
    if s.receipt=='prepared': rollback(s)

def is_original(s,v):
    return s==State(virtual=v)

for original in ('live','disabled'):
    initial=State(virtual=original)
    full=deepcopy(initial)
    snapshots=[]
    for label in steps(full): snapshots.append((label,deepcopy(full)))
    # Fault after every persisted step; repair must run on every new-kernel boot.
    for label,saved in snapshots[:-1]:
        crashed=deepcopy(saved)
        boot_repair(crashed)
        assert is_original(crashed,original),label
    installed=deepcopy(snapshots[-1][1])
    assert installed.active=='verified-redwood' and installed.backup=='apple'
    assert installed.virtual=='live' and installed.receipt=='committed'
    rollback(installed); assert is_original(installed,original)
    # Model a failure handled synchronously before launchd, including commit.
    for label,saved in snapshots:
        snapshot=deepcopy(saved)
        rollback(snapshot); assert is_original(snapshot,original),label
    # Old fallback55 performs no repair, hence cannot guarantee HAL exists.
    gap=deepcopy(initial)
    for label in steps(gap):
        if label=='original renamed and parent synced': break
    assert gap.active is None and gap.backup=='apple'
print('PASS transaction specification: 2 original VirtualAudio states, every persisted failure point')
print('CONFIRMED fallback55 leaves HAL absent at inter-rename crash; real installer still pending')
