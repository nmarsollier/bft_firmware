"""Host regression for the physical-limit clamp in motors_limits.c.

Compiles the REAL production motors_limits.c and verifies that a runtime
"set current as limit" can never widen an axis past its hard physical travel
bounds (RA ±100°, DEC -135°..+50°).  A logical limit set beyond the physical
range is clamped back to the physical bound.

Run (no hardware needed):
    cd test/host && python3 -m pytest test_limits.py -q
"""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PhysicalLimitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        lib = Path(cls.tmp.name) / "limits_sim.so"
        src = [
            "main/mount/mount_pulse_guide.c",
            "main/motors/motors_pulse_guide.c",
            "main/motors/motors_command_queue.c",
            "main/motors/motors_motion_task.c",
            "main/motors/motors_motion_stop.c",
            "main/motors/motors_motion_stop_graceful.c",
            "main/motors/motors_motion_idle.c",
            "main/motors/motors_rmt.c",
            "main/motors/motors_hw.c",
            "main/motors/motors_current_state.c",
            "main/motors/motors_enter_error_state.c",
            "main/motors/motors_microsteps.c",
            "main/motors/motors_ramp.c",
            "main/motors/motors_get_tracking_speed.c",
            "main/motors/motors_stop.c",
            "main/motors/motors_horizon.c",
            "main/motors/motors_limits.c",
            "main/utils/number_utils.c",
            "test/host/nvs_stubs.c",
            "test/host/tmc_stubs.c",
            "test/host/limits_harness.c",
            "test/host/phd_sim.c",
        ]
        subprocess.run([
            "cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
            "-I" + str(ROOT / "test/host/stubs"),
            "-I" + str(ROOT / "main"),
            "-I" + str(ROOT / "main/motors"),
            "-I" + str(ROOT / "main/config"),
            "-I" + str(ROOT / "main/tmc"),
        ] + [str(ROOT / s) for s in src] + ["-lm", "-o", str(lib)], check=True)

        cls.lib = C.CDLL(str(lib))
        cls.lib.sim_reset.argtypes = []
        cls.lib.sim_init_motion_task.argtypes = []
        cls.lib.sim_mark_idle.argtypes = []
        cls.lib.sim_set_ra_deg.argtypes = [C.c_float]
        cls.lib.sim_set_dec_deg.argtypes = [C.c_float]
        for fn in ("sim_set_ra_left_limit", "sim_set_ra_right_limit",
                   "sim_set_dec_left_limit", "sim_set_dec_right_limit"):
            getattr(cls.lib, fn).argtypes = []
            getattr(cls.lib, fn).restype = C.c_int
        for fn in ("sim_ra_min", "sim_ra_max", "sim_dec_min", "sim_dec_max"):
            getattr(cls.lib, fn).argtypes = []
            getattr(cls.lib, fn).restype = C.c_float

    def _ready(self):
        self.lib.sim_reset()
        self.lib.sim_init_motion_task()
        self.lib.sim_mark_idle()

    def test_ra_right_limit_clamped_to_physical_max(self):
        self._ready()
        self.lib.sim_set_ra_deg(150.0)
        self.assertEqual(self.lib.sim_set_ra_right_limit(), 1, "setter refused while idle")
        self.assertEqual(self.lib.sim_ra_max(), 100.0, "RA max not clamped to physical bound")

    def test_ra_left_limit_clamped_to_physical_min(self):
        self._ready()
        self.lib.sim_set_ra_deg(-150.0)
        self.assertEqual(self.lib.sim_set_ra_left_limit(), 1)
        self.assertEqual(self.lib.sim_ra_min(), -100.0)

    def test_dec_max_limit_clamped_to_physical_max(self):
        self._ready()
        self.lib.sim_set_dec_deg(80.0)
        self.assertEqual(self.lib.sim_set_dec_right_limit(), 1)
        self.assertEqual(self.lib.sim_dec_max(), 50.0)

    def test_dec_min_limit_clamped_to_physical_min(self):
        self._ready()
        self.lib.sim_set_dec_deg(-200.0)
        self.assertEqual(self.lib.sim_set_dec_left_limit(), 1)
        self.assertEqual(self.lib.sim_dec_min(), -135.0)

    def test_limit_inside_physical_range_is_kept(self):
        self._ready()
        self.lib.sim_set_ra_deg(45.0)
        self.assertEqual(self.lib.sim_set_ra_right_limit(), 1)
        self.assertEqual(self.lib.sim_ra_max(), 45.0)


if __name__ == "__main__":
    unittest.main()
