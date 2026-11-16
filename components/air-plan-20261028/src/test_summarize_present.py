import unittest
from summarize_present import summarize


def row(ms, calls, mean=2000, maximum=3000):
    return f'TEGU_PRESENT_FRAME window_ms={ms} copies={calls} mean_copy_us={mean} max_copy_us={maximum} gate=1\n'


class PresentSummaryTest(unittest.TestCase):
    def test_replay_boundary_and_weighting(self):
        text = row(9000, 1) + 'TEGU GESTURE: begin 548 steps\n'
        text += row(3000, 90) + 'interleaved process output ' + row(2000, 10, 1000)
        text += row(4000, 30, 3000, 5000) + 'TEGU GESTURE: completed\n' + row(2000, 99)
        result = summarize(text, True)
        self.assertEqual(result['calls'], 40)
        self.assertEqual(result['covered_ms'], 6000)
        self.assertEqual(result['mean_present_us'], 2500)
        self.assertEqual(result['max_present_us'], 5000)
        self.assertAlmostEqual(result['calls_per_second'], 40 / 6)

    def test_incomplete_or_replayed_capture_rejected(self):
        for text in (row(2000, 10), 'TEGU GESTURE: begin\n' + row(2000, 10),
                     'STREAM-END\n' + row(2000, 10)):
            with self.assertRaises(ValueError):
                summarize(text, True)

    def test_zero_duration_rejected(self):
        with self.assertRaises(ValueError):
            summarize(row(0, 10))


if __name__ == '__main__':
    unittest.main()
