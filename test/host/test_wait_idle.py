"""Host regression for the motion-idle wait used by the limit/home setters.

Compiles the REAL production motors code (limits, stop, motion task, RMT) and
verifies that motors_set_current_as_home() does NOT touch the position counters
when the motion task cannot be confirmed idle (the 500 ms wait times out).

This is the race MOUNT.md guards against: a delayed RMT transmission must not
let "redefine home" overwrite ra_steps/dec_steps while the motion task is still
running.

Run (no hardware needed):
    cd test/host && python3 -m pytest test_wait_idle.py -q
"""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WaitIdleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        lib = Path(cls.tmp.name) / "wait_idle_sim.so"
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
        cls.lib.sim_set_ra_deg.argtypes = [C.c_float]
        cls.lib.sim_set_dec_deg.argtypes = [C.c_float]
        cls.lib.sim_set_home.argtypes = []
        cls.lib.sim_set_home.restype = C.c_int
        cls.lib.sim_ra_steps.argtypes = []
        cls.lib.sim_ra_steps.restype = C.c_longlong
        cls.lib.sim_dec_steps.argtypes = []
        cls.lib.sim_dec_steps.restype = C.c_longlong

    def test_set_home_refused_when_motion_task_not_idle(self):
        """With the motion task not confirmed idle (the wait times out),
        set_current_as_home must refuse and leave the counters untouched."""
        self.lib.sim_reset()
        self.lib.sim_init_motion_task()   # idle semaphore count 0 → "busy"
        self.lib.sim_set_ra_deg(10.0)
        self.lib.sim_set_dec_deg(20.0)

        ra0 = self.lib.sim_ra_steps()
        dec0 = self.lib.sim_dec_steps()
        self.assertNotEqual(ra0, 0)
        self.assertNotEqual(dec0, 0)

        ok = self.lib.sim_set_home()
        self.assertEqual(ok, 0, "set_home applied despite not being idle")

        self.assertEqual(self.lib.sim_ra_steps(), ra0, "ra_steps was modified")
        self.assertEqual(self.lib.sim_dec_steps(), dec0, "dec_steps was modified")


if __name__ == "__main__":
    unittest.main()
