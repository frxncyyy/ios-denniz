#!/usr/bin/env python3
"""Compile/run synthetic host tests only; never an image/device runner."""
from pathlib import Path
import subprocess
import tempfile
import json
p = Path(__file__).resolve().parent
stage = p.parent.parent
common = [p / 'dispatcher.c', p.parent / 'capability.c',
          stage / 'main-fw-transport/wfdma/v2/command.c',
          stage / 'main-fw-transport/dma_pool_contract.c']
commands = []
with tempfile.TemporaryDirectory(prefix='nord2-capability-host-') as temp:
    for name in ('test-dispatcher', 'test-native-adapter'):
        output = Path(temp) / name
        command = ['cc', '-O1', '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-I', str(p / 'baseline'), *map(str, common),
                   str(p / (name + '.c')), '-o', str(output)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(output)], check=True,
                                capture_output=True, text=True)
        (p / (name + '.log')).write_text(result.stdout)
        print(result.stdout.splitlines()[-1])
        commands.append({'name': name, 'argv': command[:-1] + ['TEMP_BINARY'],
                         'status': 'PASS'})
with tempfile.TemporaryDirectory(prefix='nord2-capability-abi-') as temp:
    snap, canon, output = (Path(temp) / name for name in
                           ('snapshot.o', 'canonical.o', 'test-abi-layout'))
    flags = ['cc', '-O1', '-std=c11', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined']
    snap_command = flags + ['-I', str(p / 'baseline'), '-c',
                            str(p / 'abi-snapshot.c'), '-o', str(snap)]
    canon_command = flags + ['-I', str(p.parents[5] / 'shim/nord2-xnu'), '-c',
                             str(p / 'abi-canonical.c'), '-o', str(canon)]
    link_command = flags + [str(snap), str(canon),
                             str(p / 'test-abi-layout.c'), '-o', str(output)]
    for command in (snap_command, canon_command, link_command):
        subprocess.run(command, check=True)
    result = subprocess.run([str(output)], check=True, capture_output=True, text=True)
    (p / 'test-abi-layout.log').write_text(result.stdout)
    print(result.stdout.splitlines()[-1])
    commands.append({'name': 'test-abi-layout', 'argv': [snap_command, canon_command,
                     link_command], 'status': 'PASS'})
(p / 'host-test-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
