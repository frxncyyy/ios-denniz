import unittest
import subprocess
import sys
from pathlib import Path

HERE=Path(__file__).resolve().parent
PROBE=(HERE/'compositor_public_probe.c').read_text()
BUILDER=(HERE/'build_compositor_public_probe.py').read_text()

class PublicIOSurfacePresentTests(unittest.TestCase):
    def test_8tap_load_mode_is_rejected(self):
        result=subprocess.run([sys.executable,str(HERE/'build_compositor_public_probe.py'),
            '--proof-directory','/missing','--metallib','/missing','--work','/missing',
            '--stubs','/missing','--eight-tap','--load-two-draws','--alpha','0'],
            text=True,capture_output=True)
        self.assertEqual(result.returncode,2)
        self.assertIn('--eight-tap cannot be combined with --load-two-draws',result.stderr)

    def test_build_and_runtime_mode_are_opt_in_and_require_iosurface(self):
        result=subprocess.run([sys.executable,str(HERE/'build_compositor_public_probe.py'),
            '--proof-directory','/nonexistent','--metallib','/nonexistent',
            '--work','/nonexistent','--stubs','/nonexistent','--present-iosurface'],
            text=True,capture_output=True)
        self.assertEqual(result.returncode,2)
        self.assertIn('--present-iosurface requires --iosurface-target',result.stderr)
        self.assertIn("'-DTEGU_COMPOSITOR_PRESENT_IOSURFACE_PROBE'", BUILDER)
        self.assertIn("if a.present_iosurface: launch += ['--present-iosurface']", BUILDER)
        self.assertIn('argv[next_arg],"--present-iosurface"', PROBE)
        self.assertIn('argv[3],"--iosurface-target"', PROBE)

    def test_present_is_gated_after_exact_gpu_target_readback(self):
        exact_readback=PROBE.index('if(!layout_ok||unlock_bad||guard_mismatches)')
        pixel_check=PROBE.index('if(mismatches) {', exact_readback)
        present_call=PROBE.index('tegu_present_surface_and_readback(target_surface', pixel_check)
        self.assertLess(exact_readback,pixel_check)
        self.assertLess(pixel_check,present_call)
        call=PROBE[present_call:PROBE.index('goto cleanup;',present_call)]
        self.assertIn('128,128,16,16',call)
        self.assertIn('tegu_expected_bgra8,sizeof(tegu_expected_bgra8)',call)
        self.assertIn('10000,250',call)

    def test_panel_status_is_separate_from_gpu_failure_and_hold_released_only_on_match(self):
        present=PROBE[PROBE.index('if(present_iosurface) {'):PROBE.index('        rc=0;\n        goto cleanup;',PROBE.index('if(present_iosurface) {'))]
        self.assertIn('gpu_readback=PASS',present)
        self.assertIn('TEGU_PRESENT_ALREADY_MATCHED',PROBE)
        self.assertIn('TEGU_PRESENT_TIMEOUT',PROBE)
        self.assertIn('TEGU_PRESENT_READBACK_ERROR',PROBE)
        self.assertIn('hold_retained=%u',present)
        self.assertIn('if(result.status==TEGU_PRESENT_PIXELS_MATCH)',present)
        self.assertEqual(PROBE.count('tegu_present_release_hold(&present_hold)'),1)
        self.assertIn('in-process hold stays alive until this inconclusive run exits',present)
        self.assertIn('IOSURFACE_PRESENT_TERMINAL classification=%s',present)

    def test_actual_helper_is_linked_into_present_build(self):
        self.assertIn("helper_source = HERE.parent/'present'/'held_present.c'",BUILDER)
        self.assertIn("held_present.arm64.o",BUILDER)
        self.assertIn("_memcpy, _memset, _bzero, _usleep",(HERE/'libSystem-ios-arm64.tbd').read_text())
        self.assertIn("*link_inputs, str(stubs)",BUILDER)

if __name__=='__main__':unittest.main()
