import unittest
from summarize_target_cache import summarize


def row(n, hit=768, miss=256, writes=3000):
    return f'TEGU GPU TCACHE: window {n} jobs 1024 hit {hit} miss {miss} evict 4 target_writes {writes}\n'


class CacheSummaryTest(unittest.TestCase):
    def test_window_totals_and_cumulative_writes(self):
        result = summarize(row(1) + row(2, writes=4000))
        self.assertEqual(result['jobs'], 2048)
        self.assertEqual(result['hit_fraction'], .75)
        self.assertEqual(result['cumulative_target_writes_at_last_report'], 4000)
        self.assertEqual(result['missing_window_ranges'], [])

    def test_missing_and_malformed_rows_explicit(self):
        result = summarize(row(1) + 'TEGU GPU TCACHE: broken\n' + row(3))
        self.assertEqual(result['missing_window_ranges'], [[2, 2]])
        self.assertEqual(result['malformed'], 1)

    def test_invalid_accounting_or_replay_refused(self):
        for text in (row(1) * 2, row(1, miss=255), row(1) + row(2, writes=1),
                     row(1) + 'STREAM-END', ''):
            with self.subTest(text=text), self.assertRaises(ValueError):
                summarize(text)


if __name__ == '__main__':
    unittest.main()
