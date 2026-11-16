#!/usr/bin/env python3
"""Offline evidence reconciliation: never turn missing/failing evidence into PASS."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from reconcile_metal_probe import reconcile, records, BRIDGE
from metal_probe_analysis_test import fixture


def evidence():
    lines = fixture('batch-surface-v8').splitlines()
    final_memory = next(x for x in lines if b'memory phase=batch-complete ' in x)
    lines.remove(final_memory)
    lines[-2:-2] = [b'NORD2_METAL status phase=render-complete complete=104 fault=0 timeout=0 live=16 quarantine=0'] * 50
    lines.append(final_memory)
    usb = b'\n'.join(b'NORD2 GPU USER: '+x for x in lines[:-3])+b'\n'
    stdout = b'\n'.join(x for x in lines if not x.startswith(BRIDGE))+b'\n'
    return usb, stdout


class Reconcile(unittest.TestCase):
    def test_good(self):
        usb, stdout = evidence()
        combined, result, proof = reconcile(usb, stdout)
        self.assertTrue(combined.startswith(usb))
        self.assertTrue(result['batch_hardware_qualified'])
        self.assertFalse(result['ui_qualified'])
        self.assertEqual(proof['recovered_suffix_records'], 3)

    def test_stale_mixed_duplicate_or_incomplete_prefix(self):
        usb, stdout = evidence()
        variants = [stdout.replace(b'batch-surface-v8', b'batch-viewport-v7'),
                    stdout.replace(b'footprint=72000000', b'footprint=72000001', 1),
                    b'\n'.join(stdout.splitlines()[1:]),
                    stdout.replace(b'NORD2_METAL compute checked=64 mismatches=0\n', b''),
                    stdout.replace(b'NORD2_METAL compute checked=64 mismatches=0\n', b'NORD2_METAL compute checked=64 mismatches=0\n'*2),
                    stdout + b'NORD2_METAL probe start batch-surface-v8\n']
        for raw in variants:
            with self.assertRaises(ValueError):
                reconcile(usb, raw)

    def test_final_failure_not_waived(self):
        usb, stdout = evidence()
        for raw in (stdout.replace(b'failed-targets=0', b'failed-targets=1'),
                    stdout.replace(b'iosurface-direct=PASS', b'iosurface-direct=FAIL'),
                    stdout.replace(b'phase=batch-complete complete=50 fault=0', b'phase=batch-complete complete=50 fault=1'),
                    stdout.replace(b'phase=batch-complete footprint=72000000', b'phase=batch-complete footprint=300000000'),
                    b'\n'.join(stdout.splitlines()[:-1])):
            _, result, _ = reconcile(usb, raw)
            self.assertFalse(result['batch_hardware_qualified'])

    def test_old_boot_or_no_suffix(self):
        usb, stdout = evidence()
        with self.assertRaises(ValueError):
            reconcile(usb, b'\n'.join(records(usb, True))+b'\n')
        with self.assertRaises(ValueError):
            reconcile(usb+b'=== NORD2-XNU LOADER: new run, previous log preserved ===\n', stdout)


class ReconcileV9(unittest.TestCase):
    def evidence(self):
        lines = fixture('batch-features-v9').splitlines()
        local = [b'NORD2_METAL status phase=render-complete complete=104 fault=0 timeout=0 live=16 quarantine=0',
                 b'NORD2_METAL memory phase=render-complete footprint=1 resident=2 peak=3',
                 b'NORD2_METAL pixel x=0 y=0 bytes=4 raw_le=00000000ff4080bf',
                 b'NORD2_METAL feature-pixel tag=batch-cap x=1 y=2 got=00000000 want=00000000']
        stdout_lines = lines[:5] + local + lines[5:]
        kernel = [x for x in lines]
        usb = b'\n'.join(b'NORD2 GPU USER: ' + x for x in kernel[:-3]) + b'\n'
        stdout = b'\n'.join(x for x in stdout_lines if not x.startswith(BRIDGE)) + b'\n'
        return usb, stdout

    def test_v9_stdout_only_lines_do_not_break_the_prefix(self):
        usb, stdout = self.evidence()
        combined, result, proof = reconcile(usb, stdout)
        self.assertTrue(result['batch_hardware_qualified'])
        self.assertEqual(proof['recovered_suffix_records'], 3)
        self.assertNotIn(b'feature-pixel', combined)

    def test_v9_failures_not_waived(self):
        usb, stdout = self.evidence()
        _, result, _ = reconcile(usb, stdout.replace(b'features-failed=0', b'features-failed=1'))
        self.assertFalse(result['batch_hardware_qualified'])
        with self.assertRaises(ValueError):
            reconcile(usb, stdout.replace(b'batch-features-v9', b'batch-surface-v8'))


if __name__ == '__main__':
    unittest.main()
