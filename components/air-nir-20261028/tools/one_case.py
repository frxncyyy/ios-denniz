#!/usr/bin/env python3
"""Run one graphics corpus case in-process (AIR-only), for a debugger. usage: one_case.py <lib> <case-id>"""
import sys, ctypes as C
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import air_corpus as X
R, A = X.R, X.A
L = R.Libs(R.GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so', Path(sys.argv[1]))
L.m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p] * 17
L.m.tegu_mali_compile_air_pair_options.restype = C.c_int
R.live.Blob = X.RecBlob; R.specialize = X.spec; R.compile_pair = X.air_compile_pair
gpu = R.GPU; R.GPU = A.OUT / 'runs/fakegpu'
c = [x for x in R.build_cases('graphics', None) if x['id'] == sys.argv[2]][0]
R.GPU = gpu
print(R.run_graphics(L, c, R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h'), A.OUT / 'runs', False))
