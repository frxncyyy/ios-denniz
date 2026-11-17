import unittest, zlib, struct
from pathlib import Path
from parse_container import Container, Invalid
class Test(unittest.TestCase):
 def test_valid(self):
  d=Container(Path('tfa98xx.cnt').read_bytes()).decode();self.assertEqual([v['address'] for v in d['devices']],[52,55])
 def test_corrupt(self):
  b=bytearray(Path('tfa98xx.cnt').read_bytes());b[-1]^=1
  with self.assertRaises(Invalid):Container(b)
 def test_truncations(self):
  b=Path('tfa98xx.cnt').read_bytes()
  for n in range(len(b)):
   with self.assertRaises(Invalid):Container(b[:n])
 def test_pointer(self):
  b=bytearray(Path('tfa98xx.cnt').read_bytes());struct.pack_into('<I',b,46,0xffffff);struct.pack_into('<I',b,10,zlib.crc32(b[14:]))
  with self.assertRaises(Invalid):Container(b).decode()
if __name__=='__main__':unittest.main()
