#!/usr/bin/env python3
"""Preserve frozen success verification; unknown failed-run side effects stay unknown."""
from pathlib import Path
import argparse,importlib.util,json
P=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('frozen_main_evidence',P/'parse-evidence.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
def parse(data):
 result=base.parse(data)
 if not result['complete']:
  # A missing/failed final receipt cannot prove earlier DMA/RF never ran.
  result['dma_enabled']=None
  result['rf_calibration_command_executed']=None
 return result
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);a=ap.parse_args()
 result=parse(a.log.read_bytes());print(json.dumps(result,indent=2))
 raise SystemExit(0 if result['complete'] else 2)
