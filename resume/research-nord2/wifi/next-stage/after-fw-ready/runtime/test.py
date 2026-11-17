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
                   '-fsanitize=address,undefined', *map(str, common),
                   str(p / (name + '.c')), '-o', str(output)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(output)], check=True,
                                capture_output=True, text=True)
        (p / (name + '.log')).write_text(result.stdout)
        print(result.stdout.splitlines()[-1])
        commands.append({'name': name, 'argv': command[:-1] + ['TEMP_BINARY'],
                         'status': 'PASS'})
(p / 'host-test-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
