"""Host regression for the PHD2 PulseGuide path.

Compiles the REAL production guide/queue/RMT code against a simulated clock,
RTOS and RMT peripheral, then drives a guide pulse and OBSERVES the STEP
transmissions.  This is the only software layer that can catch "DEC didn't
move" independent of the firmware's own counters: a suppressed DEC RMT channel
must make the nominal DEC pulse emit zero steps, even though every layer above
returns ok=true.

Run (no hardware needed):
    cd test/host && python3 -m pytest test_phd_calibration.py -q
"""
import ctypes as C
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

# Independent oracle — never derived from the firmware's own motion functions.
SIDEREAL_DPS = 0.004178074
GUIDE_DPS = 0.5 * SIDEREAL_DPS        # 0.002089037 deg/s (NOT 0.5 deg/s)
DEG_PER_STEP = 1.8 / (64 * 500)       # 0.00005625 deg/microstep
STEP_PULSE_TICKS = 4                  # 2 us HIGH at 2 MHz
STATUS_ERROR = 4                      # MotorsStatus::MOTORS_STATUS_ERROR


def _expected_steps(duration_ms):
    return GUIDE_DPS * duration_ms / 1000.0 / DEG_PER_STEP


class SimGuideResult(C.Structure):
    _fields_ = [
        ("axis", C.c_int),
        ("ok", C.c_int),
        ("status", C.c_int),
        ("guiding", C.c_int),
        ("offset_dps", C.c_float),
        ("emitted_steps", C.c_int),
        ("ra_steps", C.c_longlong),
        ("dec_steps", C.c_longlong),
    ]


class GuideInjection(C.Structure):
    _fields_ = [
        ("direction", C.c_int),
        ("duration_ms", C.c_uint32),
        ("at_ms", C.c_uint32),
    ]


class PhdCalibrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        lib = Path(cls.tmp.name) / "phd_sim.so"
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
            "test/host/tmc_stubs.c",
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
        cls.lib.sim_set_lat.argtypes = [C.c_float]
        cls.lib.sim_set_dec_deg.argtypes = [C.c_float]
        cls.lib.sim_suppress_dec.argtypes = [C.c_int]
        cls.lib.sim_set_guide_rate_ra.argtypes = [C.c_float]
        cls.lib.sim_set_guide_rate_ra.restype = C.c_int
        cls.lib.sim_set_guide_rate_dec.argtypes = [C.c_float]
        cls.lib.sim_set_guide_rate_dec.restype = C.c_int
        cls.lib.sim_status.argtypes = []
        cls.lib.sim_status.restype = C.c_int
        cls.lib.sim_guide.argtypes = [C.c_int, C.c_uint32]
        cls.lib.sim_guide.restype = SimGuideResult
        cls.lib.sim_rmt_symbols.argtypes = [C.c_int]
        cls.lib.sim_rmt_symbols.restype = C.c_int
        cls.lib.sim_rmt_pulse_ticks.argtypes = [C.c_int]
        cls.lib.sim_rmt_pulse_ticks.restype = C.c_int
        cls.lib.sim_rmt_dir.argtypes = [C.c_int]
        cls.lib.sim_rmt_dir.restype = C.c_int
        cls.lib.sim_ra_steps.argtypes = []
        cls.lib.sim_ra_steps.restype = C.c_longlong
        cls.lib.sim_dec_steps.argtypes = []
        cls.lib.sim_dec_steps.restype = C.c_longlong
        cls.lib.sim_run_tracking_guide.argtypes = [C.c_int, C.c_uint32, C.c_uint32, C.c_uint32]
        cls.lib.sim_run_tracking_sequence.argtypes = [C.POINTER(GuideInjection), C.c_int, C.c_uint32]
        cls.lib.sim_guiding.argtypes = []
        cls.lib.sim_guiding.restype = C.c_int
        cls.lib.sim_enqueue_guide.argtypes = [C.c_int, C.c_uint32]
        cls.lib.sim_enqueue_guide.restype = C.c_int
        cls.lib.sim_motion_stop.argtypes = []
        cls.lib.sim_process_one.argtypes = []

    def setUp(self):
        self.lib.sim_reset()

    # ── mapping: direction -> axis / offset ─────────────────────────────

    def test_north_maps_to_positive_dec(self):
        r = self.lib.sim_guide(0, 500)   # north
        self.assertEqual(r.ok, 1)
        self.assertEqual(r.axis, 1)      # DEC
        self.assertGreater(r.offset_dps, 0.0)

    def test_south_maps_to_negative_dec(self):
        r = self.lib.sim_guide(1, 500)   # south
        self.assertEqual(r.ok, 1)
        self.assertEqual(r.axis, 1)
        self.assertLess(r.offset_dps, 0.0)

    def test_east_maps_to_negative_ra(self):
        r = self.lib.sim_guide(2, 500)   # east
        self.assertEqual(r.ok, 1)
        self.assertEqual(r.axis, 0)      # RA
        self.assertLess(r.offset_dps, 0.0)

    def test_west_maps_to_positive_ra(self):
        r = self.lib.sim_guide(3, 500)   # west
        self.assertEqual(r.ok, 1)
        self.assertEqual(r.axis, 0)
        self.assertGreater(r.offset_dps, 0.0)

    def test_north_at_pole_rejected(self):
        self.lib.sim_set_dec_deg(0.0)    # D = 0 (celestial pole)
        r = self.lib.sim_guide(0, 500)
        self.assertEqual(r.ok, 0)
        self.assertEqual(r.axis, -1)

    # ── DEC actually emits STEP pulses ──────────────────────────────────

    def test_dec_pulse_emits_steps(self):
        r = self.lib.sim_guide(0, 500)   # north, 500 ms
        expected = _expected_steps(500)  # ~9.28 steps
        self.assertGreaterEqual(r.emitted_steps, math.floor(expected) - 1)
        self.assertLessEqual(r.emitted_steps, math.ceil(expected) + 1)
        # Observed RMT transmission matches the counter.
        self.assertEqual(self.lib.sim_rmt_symbols(1), r.emitted_steps)
        # Pulse shape: 12 ticks HIGH.
        self.assertEqual(self.lib.sim_rmt_pulse_ticks(1), STEP_PULSE_TICKS)

    def test_suppressed_dec_rmt_fails(self):
        """A motionless DEC (RMT TX suppressed) MUST fail, even though the
        guide command is accepted and enqueued.  This is the calibration bug
        the suite exists to catch."""
        self.lib.sim_suppress_dec(1)
        r = self.lib.sim_guide(0, 500)
        self.assertEqual(r.ok, 1)              # accepted, enqueued...
        self.assertEqual(r.emitted_steps, 0)   # ...but DEC never moved
        self.assertEqual(self.lib.sim_rmt_symbols(1), 0)
        # The RMT failure is published as a hardware fault, not a clean "done".
        self.assertEqual(r.status, STATUS_ERROR)
        self.assertEqual(r.guiding, 0)         # no stale guide deadline re-asserts

    def test_dec_limit_rejected(self):
        """A DEC guide that would exceed the mechanical limit is rejected
        upfront, not silently truncated into a partial (or zero) move."""
        self.lib.sim_set_dec_deg(150.0)        # at the DEC limit
        r = self.lib.sim_guide(0, 500)         # north would push past +150
        self.assertEqual(r.ok, 0)
        self.assertEqual(r.axis, -1)

    def test_dec_guide_during_tracking(self):
        """A DEC guide while sidereal tracking is active still moves DEC."""
        dec0 = self.lib.sim_dec_steps()
        ra0 = self.lib.sim_ra_steps()
        self.lib.sim_run_tracking_guide(0, 500, 1000, 2000)  # north 500 ms at t=1 s
        dec_delta = self.lib.sim_dec_steps() - dec0
        ra_delta = self.lib.sim_ra_steps() - ra0
        expected_dec = _expected_steps(500)                 # ~9.28 steps
        assert abs(dec_delta - expected_dec) <= 2, f"DEC {dec_delta} != ~{expected_dec:.1f}"
        expected_ra = SIDEREAL_DPS * 2.0 / DEG_PER_STEP     # ~74.3 steps
        assert abs(ra_delta - expected_ra) <= 10, f"RA {ra_delta} != ~{expected_ra:.1f}"

    def _run_schedule(self, directions, duration_ms, gap_ms):
        """Run a schedule of same-duration pulses during continuous tracking."""
        arr = (GuideInjection * len(directions))()
        at = 0
        for i, d in enumerate(directions):
            arr[i] = GuideInjection(d, duration_ms, at)
            at += duration_ms + gap_ms
        total_ms = len(directions) * (duration_ms + gap_ms)
        self.lib.sim_run_tracking_sequence(arr, len(directions), total_ms)

    def test_full_calibration_sequence(self):
        """The full W→E→cleanup→N→S calibration runs as ONE uninterrupted
        tracking session (no STOP/restart between phases), and DEC ends at +2
        cleanup pulses above the origin."""
        dec0 = self.lib.sim_dec_steps()
        ra0 = self.lib.sim_ra_steps()
        # One continuous run: 12 W, 12 E, 3 cleanup N, 11 N, 12 S.
        directions = [3] * 12 + [2] * 12 + [0] * 3 + [0] * 11 + [1] * 12
        self._run_schedule(directions, 500, 1000)

        # Net DEC = (3 + 11) north - 12 south = +2 north ≈ +18.6 steps.
        dec_delta = self.lib.sim_dec_steps() - dec0
        assert abs(dec_delta - 2 * _expected_steps(500)) <= 3, \
            f"DEC net {dec_delta} != ~{2 * _expected_steps(500):.1f}"
        # RA: W and E corrections cancel; the axis keeps tracking (positive).
        assert self.lib.sim_ra_steps() > ra0, "RA did not keep tracking"

    def test_substep_fraction_accumulation(self):
        """20 × 10 ms north pulses integrate to ~7.4 steps, not 0 or 20."""
        dec0 = self.lib.sim_dec_steps()
        self._run_schedule([0] * 20, 10, 100)     # sub-step pulses
        dec_delta = self.lib.sim_dec_steps() - dec0
        expected = 20 * _expected_steps(10)       # ~7.4
        assert 4 <= dec_delta <= 11, f"sub-step sum {dec_delta} != ~{expected:.2f}"

    def _run_entries(self, entries, total_ms):
        """Run an arbitrary schedule of (direction, duration_ms, at_ms) entries."""
        arr = (GuideInjection * len(entries))()
        for i, (d, dur, at) in enumerate(entries):
            arr[i] = GuideInjection(d, dur, at)
        self.lib.sim_run_tracking_sequence(arr, len(entries), total_ms)

    def test_axis_overlap(self):
        """north 1000 ms + west 500 ms (200 ms later): both axes move, guiding
        only ends once both deadlines pass."""
        dec0 = self.lib.sim_dec_steps()
        ra0 = self.lib.sim_ra_steps()
        self._run_entries([(0, 1000, 0), (3, 500, 200)], 2000)
        dec_delta = self.lib.sim_dec_steps() - dec0
        assert abs(dec_delta - _expected_steps(1000)) <= 2, \
            f"DEC {dec_delta} != ~{_expected_steps(1000):.1f}"
        ra_delta = self.lib.sim_ra_steps() - ra0
        expected_ra = SIDEREAL_DPS * 2.0 / DEG_PER_STEP + _expected_steps(500)
        assert abs(ra_delta - expected_ra) <= 3, f"RA {ra_delta} != ~{expected_ra:.1f}"
        assert self.lib.sim_guiding() == 0, "guiding stuck true after overlap"

    def test_same_axis_overlap_same_direction_extends(self):
        """Two north 500 ms pulses 200 ms apart: the deadline extends, not sums."""
        dec0 = self.lib.sim_dec_steps()
        self._run_entries([(0, 500, 0), (0, 500, 200)], 1000)
        dec_delta = self.lib.sim_dec_steps() - dec0
        # extends to t=700 ms (~13 steps), not 1000 ms (~18.6)
        assert abs(dec_delta - _expected_steps(700)) <= 2, \
            f"DEC {dec_delta} != ~{_expected_steps(700):.1f}"

    def test_same_axis_overlap_opposite_replaces(self):
        """north 500 ms then south 500 ms 200 ms later: south replaces north."""
        dec0 = self.lib.sim_dec_steps()
        self._run_entries([(0, 500, 0), (1, 500, 200)], 1000)
        dec_delta = self.lib.sim_dec_steps() - dec0
        expected = _expected_steps(200) - _expected_steps(500)  # ~-5.6
        assert abs(dec_delta - expected) <= 2, f"DEC {dec_delta} != ~{expected:.1f}"

    def test_stop_discards_pending_guide(self):
        """A guide enqueued before a STOP is discarded (generation bump), not run."""
        self.assertEqual(self.lib.sim_enqueue_guide(0, 500), 1)  # north enqueued
        self.lib.sim_motion_stop()                               # bump generation
        dec0 = self.lib.sim_dec_steps()
        self.lib.sim_process_one()                               # discard the stale guide
        assert self.lib.sim_dec_steps() == dec0, "stale guide ran after STOP"

    # ── guide-rate validation ─────────────────────────────────────────

    def test_guide_rate_validation(self):
        """Rates outside [0,1] (or non-finite) are rejected and not applied."""
        self.assertEqual(self.lib.sim_set_guide_rate_dec(-0.1), 0)
        self.assertEqual(self.lib.sim_set_guide_rate_dec(1.1), 0)
        self.assertEqual(self.lib.sim_set_guide_rate_dec(float("nan")), 0)
        self.assertEqual(self.lib.sim_set_guide_rate_dec(float("inf")), 0)
        # 0 (disabled) and 1.0 are valid.
        self.assertEqual(self.lib.sim_set_guide_rate_dec(0.0), 1)
        self.assertEqual(self.lib.sim_set_guide_rate_dec(1.0), 1)

    def test_zero_guide_rate_disables_axis(self):
        """rate=0 disables guiding on that axis only; the other axis works."""
        self.assertEqual(self.lib.sim_set_guide_rate_dec(0.0), 1)
        r = self.lib.sim_guide(0, 500)   # north -> DEC
        self.assertEqual(r.ok, 0)        # rejected: DEC guide disabled
        self.assertEqual(r.axis, -1)
        # RA (west) is still usable.
        r = self.lib.sim_guide(3, 500)   # west -> RA
        self.assertEqual(r.ok, 1)
        self.assertEqual(r.axis, 0)

    def test_ra_pulse_emits_steps(self):
        r = self.lib.sim_guide(3, 500)   # west, 500 ms
        expected = _expected_steps(500)
        self.assertGreaterEqual(r.emitted_steps, math.floor(expected) - 1)
        self.assertLessEqual(r.emitted_steps, math.ceil(expected) + 1)
        self.assertEqual(self.lib.sim_rmt_symbols(0), r.emitted_steps)

    # ── magnitude scales with duration (100..5000 ms) ───────────────────

    def test_magnitude_scales_with_duration(self):
        for duration in (100, 250, 500, 750, 1000, 2000, 5000):
            r = self.lib.sim_guide(0, duration)
            expected = _expected_steps(duration)
            self.assertGreaterEqual(r.emitted_steps, math.floor(expected) - 1,
                                    f"{duration} ms")
            self.assertLessEqual(r.emitted_steps, math.ceil(expected) + 1,
                                 f"{duration} ms")


if __name__ == "__main__":
    unittest.main()
