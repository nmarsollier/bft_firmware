"""Host regression for STOP position integrity under RMT interruption.

Compiles the REAL production motion loop + RMT code against a simulated
clock/RTOS/RMT peripheral that models in-flight batches (deferred TX-done), then
drives a slew and interrupts it mid-flight.  This is the only software layer
that can compare the firmware's *reported* step counters against the steps the
RMT peripheral *physically emitted*, independent of the firmware's own counters.

Run (no hardware needed):
    cd test/host && python3 -m pytest test_motion_abort.py -q
"""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MotionAbortTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        lib = Path(cls.tmp.name) / "motion_abort_sim.so"
        src = [
            "main/mount/mount_pulse_guide.c",
            "main/motors/motors_pulse_guide.c",
            "main/motors/motors_command_queue.c",
            "main/motors/motors_motion_task.c",
            "main/motors/motors_rmt.c",
            "main/motors/motors_hw.c",
            "main/motors/motors_current_state.c",
            "main/motors/motors_enter_error_state.c",
            "main/motors/motors_get_tracking_speed.c",
            "main/motors/motors_stop.c",
            "main/motors/motors_horizon.c",
            "test/host/phd_sim.c",
        ]
        subprocess.run([
            "cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
            "-I" + str(ROOT / "test/host/stubs"),
            "-I" + str(ROOT / "main"),
            "-I" + str(ROOT / "main/motors"),
            "-I" + str(ROOT / "main/config"),
        ] + [str(ROOT / s) for s in src] + ["-lm", "-o", str(lib)], check=True)

        cls.lib = C.CDLL(str(lib))
        cls.lib.sim_reset.argtypes = []
        cls.lib.sim_defer_done.argtypes = [C.c_int]
        cls.lib.sim_arm_stop.argtypes = [C.c_int, C.c_int]
        cls.lib.sim_enqueue_slew_ra.argtypes = [C.c_float]
        cls.lib.sim_process_one.argtypes = []
        cls.lib.sim_ra_steps.argtypes = []
        cls.lib.sim_ra_steps.restype = C.c_longlong
        cls.lib.sim_dec_steps.argtypes = []
        cls.lib.sim_dec_steps.restype = C.c_longlong
        cls.lib.sim_phys_emitted.argtypes = [C.c_int]
        cls.lib.sim_phys_emitted.restype = C.c_longlong
        cls.lib.sim_position_uncertain.argtypes = []
        cls.lib.sim_position_uncertain.restype = C.c_int
        cls.lib.sim_set_dec_deg.argtypes = [C.c_float]
        cls.lib.motors_slew_duration_estimate.argtypes = [C.c_float, C.c_float]
        cls.lib.motors_slew_duration_estimate.restype = C.c_float
        cls.lib.sim_enqueue_raw_dec_guide.argtypes = [C.c_float, C.c_uint32]

    def setUp(self):
        self.lib.sim_reset()
        self.lib.sim_defer_done(1)

    def test_graceful_stop_preserves_position(self):
        """A normal STOP mid-slew drains the committed batches, so the reported
        position equals the physically emitted steps (no silent drift)."""
        self.lib.sim_enqueue_slew_ra(10.0)
        self.lib.sim_arm_stop(0, 5)   # graceful (motors_stop) after 5 transmits
        self.lib.sim_process_one()
        self.assertEqual(self.lib.sim_ra_steps(), self.lib.sim_phys_emitted(0))

    def test_emergency_stop_marks_uncertain(self):
        """An emergency abort mid-slew loses the in-flight batch: the reported
        position falls behind the physically emitted steps and is flagged."""
        self.lib.sim_enqueue_slew_ra(10.0)
        self.lib.sim_arm_stop(1, 5)   # emergency (motors_motion_stop) after 5 transmits
        self.lib.sim_process_one()
        self.assertLess(self.lib.sim_ra_steps(), self.lib.sim_phys_emitted(0))
        self.assertEqual(self.lib.sim_position_uncertain(), 1)

    def test_slew_duration_estimate_accounts_for_ramp(self):
        """The ramp-aware duration is slower than a constant velocity, matches
        the constant MIN_SLEW_DPS for short moves, and stays bounded."""
        d = self.lib.motors_slew_duration_estimate
        # 60° at 4°/s is a trapezoid: accel(10°) + cruise(40°) + decel(10°).
        t = d(60.0, 4.0)
        self.assertGreater(t, 60.0 / 4.0)           # ramps add time
        self.assertLess(t, 60.0 / 4.0 + 10.0)       # bounded overhead
        # Below SHORT_SLEW (2°) the axis runs at constant 0.8°/s.
        self.assertAlmostEqual(d(1.0, 4.0), 1.0 / 0.8, delta=0.01)
        # Degenerate inputs return 0.
        self.assertEqual(d(0.0, 4.0), 0.0)
        self.assertEqual(d(10.0, 0.0), 0.0)

    def test_dec_guide_crossing_pole_rejected_at_execution(self):
        """A DEC guide that would cross the pole is rejected by the motion task
        at execution (using the live DEC position), even when enqueued raw —
        bypassing the mount layer's own validation."""
        self.lib.sim_set_dec_deg(0.1)   # just above the pole (D=0)
        dec0 = self.lib.sim_dec_steps()
        self.lib.sim_enqueue_raw_dec_guide(-0.5, 500)   # arc -0.25° crosses D=0
        self.lib.sim_process_one()
        self.assertEqual(self.lib.sim_dec_steps(), dec0)  # no DEC motion


if __name__ == "__main__":
    unittest.main()
