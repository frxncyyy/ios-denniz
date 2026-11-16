#!/usr/bin/env python3
import unittest
from summarize_memclk import summarize

ROW = 'TEGU MEMCLK: job 1 token 42 mif_khz 3172000 rc 0 us 12 busy 1 int_khz 533000 rc 0 us 10 busy 0 stopped 0'


class Samples(unittest.TestCase):
    def test_completion_and_overhead(self):
        report = summarize(ROW)
        self.assertEqual(report['mif']['busy_rates_khz'], [3172000])
        self.assertEqual(report['interconnect']['busy_samples'], 0)
        self.assertEqual(report['interconnect']['completed_during_or_before_query'], 1)
        self.assertEqual(report['ipc_query_us'], 22)

    def test_invalid_never_becomes_rate(self):
        report = summarize(ROW.replace('rc 0', 'rc -12').replace('stopped 0', 'stopped 1'))
        self.assertEqual(report['mif']['busy_rates_khz'], [])
        self.assertEqual(report['mif']['invalid'], 1)
        self.assertEqual(report['stopped'], 1)

    def test_replayed_samples_refused(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            summarize(ROW + '\n' + ROW)

    def test_truncated_and_empty(self):
        self.assertEqual(summarize(ROW[:60])['malformed'], 1)
        self.assertEqual(summarize('')['mif']['busy_samples'], 0)


if __name__ == '__main__':
    unittest.main()
