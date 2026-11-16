import unittest
from summarize_sysclk import summarize

ROW = 'TEGU SYSCLK: job 1 token 42 cpu 0x80000100 domain 2 khz 1425000 rc 0 us 55 gpu_busy 1'


class Samples(unittest.TestCase):
    def test_domains_are_separate(self):
        report = summarize(ROW + '\n' + ROW.replace('domain 2', 'domain 3').replace('1425000', '1795000'))
        self.assertEqual(report['jobs'], 1)
        self.assertEqual(report['ipc_query_us'], 110)
        self.assertEqual(report['domains']['cpucl0']['rates_khz'], [1425000])
        self.assertEqual(report['domains']['cpucl1']['rates_khz'], [1795000])

    def test_error_and_zero_are_not_rates(self):
        for row in (ROW.replace('rc 0', 'rc -11'), ROW.replace('1425000', '0')):
            result = summarize(row)['domains']['cpucl0']
            self.assertEqual(result['rates_khz'], [])
            self.assertEqual(result['invalid'], 1)

    def test_completed_gpu_still_valid_clock(self):
        result = summarize(ROW.replace('gpu_busy 1', 'gpu_busy 0'))['domains']['cpucl0']
        self.assertEqual(result['valid_samples'], 1)
        self.assertEqual(result['gpu_unfinished_at_query_end'], 0)

    def test_duplicate_capture_refused(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            summarize(ROW + '\n' + ROW)

    def test_missing_and_corrupt(self):
        self.assertEqual(summarize('')['samples'], 0)
        for row in (ROW[:60], ROW.replace('domain 2', 'domain 99'), ROW + 'corrupt'):
            self.assertEqual(summarize(row)['malformed'], 1)


if __name__ == '__main__':
    unittest.main()
