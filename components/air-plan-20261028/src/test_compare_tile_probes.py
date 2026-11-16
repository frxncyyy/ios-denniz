import unittest
from compare_tile_probes import compare, probes
ROW = 'TEGU_TILE_PROBE case=0 size=64x64 draws=2 duplicate=0 clear=0 half=0 rc=0 differing=0 bad=0 refbad=0 maxdiff=0 guards=0 changed=16384 ref_us=100 tile_us=50\n'
END = 'TEGU_TILE_PROBE end cases=1 failed=0 error=0 result=PASS\n'
class Comparison(unittest.TestCase):
    def test_comparison(self):
        self.assertEqual(compare(ROW+END, ROW.replace('tile_us=50','tile_us=25')+END)[ 'cases'][0]['ratio'], .5)
    def test_replayed_incomplete_or_inexact(self):
        for text in (ROW+ROW+END, ROW, END, ROW.replace('differing=0','differing=1')+END):
            with self.assertRaises(ValueError):probes(text)
    def test_truncated_explicitly_excluded(self):
        rows, excluded=probes(ROW.replace('tile_us=50','')+END)
        self.assertEqual(len(rows),0)
        self.assertEqual(excluded[0]['fields'],['tile_us'])
    def test_changed_fixture(self):
        with self.assertRaisesRegex(ValueError,'fixture'):
            compare(ROW+END,ROW.replace('draws=2','draws=3')+END)
if __name__=='__main__':unittest.main()
