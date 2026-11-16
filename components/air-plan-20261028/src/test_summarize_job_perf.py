import unittest
from summarize_job_perf import samples


class JobSamplesTest(unittest.TestCase):
    first = 'TEGU TGJ2 TIME: job 64 serial 9 rc 0x0 target 1125x2436 row 4608 total 1000 prep 40 us\n'
    second = 'TEGU TGJ2 TIME2: job 64 lock 0 gpu 800 unmap 40 us heap_kb 1\n'
    heap = 'TEGU TGJ HEAP: job 64 known 1 head 128 tail 960 used 1024 bytes\n'

    def test_pair_and_heap(self):
        # Completion order can differ from record order between callers.
        r = samples(self.second + self.heap + self.first)[0]
        self.assertEqual((r['non_gpu'], r['heap_used']), (200, 1024))

    def test_incomplete_failed_or_duplicate(self):
        self.assertEqual(samples(self.first), [])
        self.assertEqual(samples(self.first.replace('rc 0x0', 'rc 0x5') + self.second), [])
        with self.assertRaises(ValueError):
            samples(self.first + self.second + self.first)

    def test_replay_excludes_startup(self):
        body = 'TEGU GESTURE: begin 548 steps\n' + self.first + self.second + 'TEGU GESTURE: completed\n'
        self.assertEqual(len(samples(self.first + body, replay=True)), 1)
        with self.assertRaises(ValueError):
            samples(self.first, replay=True)

    def test_bad_heap_refused(self):
        with self.assertRaises(ValueError):
            samples(self.first + self.second + self.heap.replace('used 1024', 'used 1025'))


if __name__ == '__main__':
    unittest.main()
