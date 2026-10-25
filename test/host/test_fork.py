"""Execute production C coordinate/guide math without touching hardware.

The conversions are now pure functions of (position, site, time), so the tests
pass latitude/longitude/time explicitly and verify:
  - forward (axis -> equatorial) against an independent vector oracle,
  - reverse (equatorial -> axis) minimum-travel selection, both hemispheres,
  - flip symmetry (same pointing),
  - pole handling (both poles + equivalent representations),
  - guiding signs derived from the current mechanical position,
  - mechanical limits and periodic equivalents.
"""
import ctypes as C
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NOW = 1780000000
LAT_SOUTH = -32.89
LAT_NORTH = 33.0


class Axis(C.Structure):
    _fields_ = [("ra", C.c_float), ("dec", C.c_float)]


class Eq(C.Structure):
    _fields_ = [("ra", C.c_float), ("dec", C.c_float)]


class ForkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        lib = Path(cls.tmp.name) / "mount.so"
        subprocess.run([
            "cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Dgettimeofday=host_gettimeofday",
            "-I" + str(ROOT / "test/host/stubs"), "-I" + str(ROOT / "main"),
            "-I" + str(ROOT / "main/motors"),
            str(ROOT / "main/mount/mount_coordinate_tools.c"),
            str(ROOT / "main/mount/mount_get_lst.c"),
            str(ROOT / "main/mount/mount_pulse_guide.c"),
            str(ROOT / "main/mount/mount_slew_to_coordinates.c"),
            str(ROOT / "test/host/mount_stubs.c"), "-lm", "-o", str(lib),
        ], check=True)
        cls.lib = C.CDLL(str(lib))
        cls.lib.host_reset.argtypes = [C.c_long]
        cls.lib.host_limits.argtypes = [C.c_float] * 4
        cls.lib.host_lat.argtypes = [C.c_float]
        cls.lib.host_dec.argtypes = [C.c_float]
        cls.lib.host_ra.argtypes = [C.c_float]
        cls.lib.host_time.argtypes = [C.c_double]
        cls.lib.host_slew.argtypes = [C.c_float, C.c_float, C.c_int]
        cls.lib.host_slew.restype = C.c_bool
        cls.lib.axis_to_equatorial.argtypes = [Axis, C.c_float, C.c_float, C.c_double]
        cls.lib.axis_to_equatorial.restype = Eq
        cls.lib.equatorial_to_axis.argtypes = [Eq, Axis, C.c_float, C.c_float,
                                               C.c_double, C.POINTER(Axis)]
        cls.lib.equatorial_to_axis.restype = C.c_bool
        cls.lib.host_guide.argtypes = [C.c_int]
        cls.lib.host_guide.restype = C.c_bool
        cls.lib.mount_set_guide_rate_dec.argtypes = [C.c_float]

    def setUp(self):
        self.lib.host_reset(NOW)
        self.lib.host_lat(LAT_SOUTH)
        # Independent sidereal angle at the fixed UTC instant (USNO formula).
        jd = NOW / 86400 + 2440587.5
        t = (jd - 2451545) / 36525
        self.lst = (280.46061837 + 360.98564736629 * (jd - 2451545)
                    + .000387933 * t * t - t * t * t / 38710000) % 360 / 15

    # ── helpers ─────────────────────────────────────────────────────────
    def forward(self, ra_deg, dec_deg, lat=LAT_SOUTH):
        return self.lib.axis_to_equatorial(Axis(ra_deg, dec_deg), lat, 0.0, NOW)

    def oracle_forward(self, ra_deg, dec_deg, lat=LAT_SOUTH):
        p = 1.0 if lat > 0 else -1.0
        R = math.radians(ra_deg)
        D = math.radians(dec_deg)
        sD = math.sin(D)
        x = sD * math.cos(R)
        y = sD * math.sin(R)
        z = p * math.cos(D)
        dec = math.degrees(math.atan2(z, math.hypot(x, y)))
        if abs(sD) < 1e-6:
            ra = (self.lst - ra_deg / 15.0) % 24.0
        else:
            H = math.degrees(math.atan2(y, x))
            ra = (self.lst - H / 15.0) % 24.0
        return ra, dec

    def reverse(self, ra_hours, dec_deg, current, lat=LAT_SOUTH):
        out = Axis(999, 999)
        ok = self.lib.equatorial_to_axis(Eq(ra_hours, dec_deg), current,
                                         lat, 0.0, NOW, C.byref(out))
        return ok, out

    def ha_ra(self, ha_deg):
        """RA hours whose hour angle is ha_deg at the fixed instant."""
        return (self.lst - ha_deg / 15.0) % 24.0

    # ── forward ─────────────────────────────────────────────────────────
    def test_forward_matches_oracle_south(self):
        for ra in [-140, -60, -30, 0, 30, 60, 140]:
            for dec in [-150, -30, -1, 0, 1, 30, 90, 149]:
                got = self.forward(ra, dec)
                era, edec = self.oracle_forward(ra, dec)
                self.assertAlmostEqual(got.ra, era, delta=1e-3)
                self.assertAlmostEqual(got.dec, edec, delta=1e-3)

    def test_forward_known_points_south(self):
        # D=90 is the equator (dec=0), H = R.
        for ra, H in [(0, 0.0), (30, 30.0), (-30, -30.0)]:
            got = self.forward(ra, 90)
            self.assertAlmostEqual(got.dec, 0.0, delta=1e-4)
            self.assertAlmostEqual(got.ra, (self.lst - H / 15.0) % 24, delta=1e-3)
        # D=0 is the south pole (dec=-90), RA by convention LST - R/15.
        self.assertAlmostEqual(self.forward(0, 0).dec, -90.0, delta=1e-4)
        self.assertAlmostEqual(self.forward(60, 0).ra, (self.lst - 4.0) % 24, delta=1e-3)
        # D=57 at lat -33 is the zenith (dec = -33).
        self.assertAlmostEqual(self.forward(0, 57).dec, -33.0, delta=1e-3)
        # R=60, D=-30 -> dec=-60, H=-120.
        got = self.forward(60, -30)
        self.assertAlmostEqual(got.dec, -60.0, delta=1e-3)
        self.assertAlmostEqual(got.ra, (self.lst + 8.0) % 24, delta=1e-3)

    def test_forward_matches_oracle_north(self):
        for ra in [-140, -60, 0, 60, 140]:
            for dec in [-150, -30, 0, 30, 90, 149]:
                got = self.forward(ra, dec, LAT_NORTH)
                era, edec = self.oracle_forward(ra, dec, LAT_NORTH)
                self.assertAlmostEqual(got.ra, era, delta=1e-3)
                self.assertAlmostEqual(got.dec, edec, delta=1e-3)

    def test_forward_known_points_north(self):
        self.assertAlmostEqual(self.forward(0, 0, LAT_NORTH).dec, 90.0, delta=1e-4)
        self.assertAlmostEqual(self.forward(0, 90, LAT_NORTH).dec, 0.0, delta=1e-4)
        self.assertAlmostEqual(self.forward(0, 57, LAT_NORTH).dec, 33.0, delta=1e-3)

    def test_lat_zero_is_south(self):
        self.assertAlmostEqual(self.forward(0, 0, 0.0).dec, -90.0, delta=1e-4)

    # ── flip equivalence ──────────────────────────────────────────────
    def test_flip_equivalents_point_at_same_sky(self):
        for ra in [-140, -60, 0, 60, 140]:
            for dec in [1, 30, 90, 149]:
                a = self.forward(ra, dec)
                b = self.forward(ra + 180, -dec)
                self.assertAlmostEqual(a.dec, b.dec, delta=1e-3)
                self.assertAlmostEqual(a.ra, b.ra, delta=1e-3)

    # ── reverse selection ───────────────────────────────────────────────
    def test_goto_current_object_stays_put(self):
        # From (60,-30) the object (dec=-60, H=-120) is itself: the flipped equivalent wins.
        eq = self.forward(60, -30)
        ok, out = self.reverse(eq.ra, eq.dec, Axis(60, -30))
        self.assertTrue(ok)
        self.assertAlmostEqual(out.ra, 60, delta=1e-3)
        self.assertAlmostEqual(out.dec, -30, delta=1e-3)

    def test_goto_south_pole_keeps_ra(self):
        ok, out = self.reverse(self.ha_ra(0), -90, Axis(75, 30))
        self.assertTrue(ok)
        self.assertAlmostEqual(out.ra, 75, delta=1e-3)
        self.assertAlmostEqual(out.dec, 0, delta=1e-3)

    def test_goto_north_pole_not_confused_with_south(self):
        # Southern mount: north pole (dec=+90) is D ≡ 180 (mod 360), never D=0.
        self.lib.host_limits(-200, 200, -200, 200)
        ok, out = self.reverse(self.ha_ra(0), 90, Axis(40, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(out.ra, 40, delta=1e-3)
        # D=+180 and D=-180 are the same (opposite) pole; the point is it is
        # not D=0 (the reference/south pole).
        self.assertAlmostEqual(abs((out.dec - 180) % 360), 0, delta=1e-3)

    def test_goto_north_pole_rejected_without_range(self):
        # DEC limits do not include D=180: reject, not silently D=0.
        self.lib.host_limits(-150, 150, -150, 150)
        ok, _ = self.reverse(self.ha_ra(0), 90, Axis(40, 0))
        self.assertFalse(ok)

    def test_pole_round_trip_no_history(self):
        # (60,-30) -> pole -> same object recovers (60,-30) by min travel.
        eq = self.forward(60, -30)
        ok, at_pole = self.reverse(eq.ra, -90, Axis(60, -30))
        self.assertTrue(ok)
        self.assertAlmostEqual(at_pole.ra, 60, delta=1e-3)
        self.assertAlmostEqual(at_pole.dec, 0, delta=1e-3)
        ok, back = self.reverse(eq.ra, eq.dec, Axis(60, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(back.ra, 60, delta=1e-3)
        self.assertAlmostEqual(back.dec, -30, delta=1e-3)

    def test_reject_out_of_limits(self):
        # H=160, dec=-60, limits RA ±90 DEC -20..120 -> reject.
        self.lib.host_limits(-90, 90, -20, 120)
        ok, _ = self.reverse(self.ha_ra(160), -60, Axis(0, 0))
        self.assertFalse(ok)

    def test_admit_negative_side(self):
        # H=160, dec=-80 -> R=-20, D=-10.
        self.lib.host_limits(-90, 90, -20, 120)
        ok, out = self.reverse(self.ha_ra(160), -80, Axis(0, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(out.ra, -20, delta=1e-3)
        self.assertAlmostEqual(out.dec, -10, delta=1e-3)

    def test_admit_with_wider_limits(self):
        self.lib.host_limits(-150, 150, -150, 150)
        ok, out = self.reverse(self.ha_ra(160), -60, Axis(0, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(out.ra, -20, delta=1e-3)
        self.assertAlmostEqual(out.dec, -30, delta=1e-3)

    def test_periodicity_prefers_2_degree_travel(self):
        # R=179, target equivalent -179, limits admit 181: move 2°, not 358°.
        self.lib.host_limits(-180, 181, -150, 150)
        ok, out = self.reverse(self.ha_ra(-179), 0, Axis(179, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(out.ra, 181, delta=1e-3)

    def test_small_correction_no_180_jump(self):
        # Crossing H=0 far from the pole stays a small move (unflipped).
        ok, a = self.reverse(self.ha_ra(-0.5), -60, Axis(0, 30))
        self.assertTrue(ok)
        ok, b = self.reverse(self.ha_ra(0.5), -60, a)
        self.assertTrue(ok)
        self.assertLess(abs(b.ra - a.ra), 2.0)
        self.assertAlmostEqual(b.dec, 30, delta=1e-3)

    def test_meridian_cross_same_instant_uses_min_travel(self):
        # West (H=+60) then east (H=-60) at the same instant: from (60,30) the
        # min-travel destination is the flipped (120,-30), not the unflipped (-60,30).
        self.lib.host_limits(-150, 150, -150, 150)
        ok, west = self.reverse(self.ha_ra(60), -60, Axis(0, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(west.ra, 60, delta=1e-3)
        self.assertAlmostEqual(west.dec, 30, delta=1e-3)
        ok, east = self.reverse(self.ha_ra(-60), -60, west)
        self.assertTrue(ok)
        self.assertAlmostEqual(east.ra, 120, delta=1e-3)
        self.assertAlmostEqual(east.dec, -30, delta=1e-3)

    def test_invalid_inputs_rejected(self):
        bad = [Eq(math.nan, 0), Eq(math.inf, 0), Eq(0, math.nan),
               Eq(0, math.inf), Eq(0, -91), Eq(0, 91)]
        for eq in bad:
            self.assertFalse(self.lib.equatorial_to_axis(eq, Axis(), LAT_SOUTH,
                                                         0.0, NOW, C.byref(Axis())))

    # ── GOTO (mount_slew_to_coordinates) ──────────────────────────────
    def _slew(self, ra_hours, dec_deg, speed=4):
        ok = self.lib.host_slew(ra_hours, dec_deg, speed)
        called = C.c_int.in_dll(self.lib, "slew_called").value
        ra = C.c_float.in_dll(self.lib, "slew_ra").value
        dec = C.c_float.in_dll(self.lib, "slew_dec").value
        return ok, called, ra, dec

    @staticmethod
    def _quant(deg):
        """Degrees -> absolute microstep counter (mirrors motors_deg_to_steps)."""
        step = 1.8 / (32 * 500)
        return int(deg / step + (0.5 if deg >= 0 else -0.5))

    def test_goto_alternative_when_nearest_out_of_limit(self):
        # Reproduce: current (149.99, 30), target H=149.999 dec=-60, limits ±150.
        # The unflipped lands at R≈150.007 after compensation (past the limit); the
        # minimum-travel selection must fall back to the flipped (≈-29.867, -30).
        self.lib.host_limits(-150, 150, -150, 150)
        self.lib.host_ra(149.99)
        self.lib.host_dec(30)
        ok, called, ra, dec = self._slew(self.ha_ra(149.999), -60)
        self.assertTrue(ok)
        self.assertEqual(called, 1)
        self.assertTrue(-150 <= ra <= 150, ra)
        self.assertAlmostEqual(dec, -30, delta=0.5)
        got = self.forward(ra, dec)
        self.assertAlmostEqual(got.dec, -60, delta=0.5)

    def test_goto_rejects_without_motion(self):
        # H=160 dec=-60 with limits RA ±90 DEC -20..120 has no valid solution:
        # reject and never call the motor layer.
        self.lib.host_limits(-90, 90, -20, 120)
        self.lib.host_ra(0)
        self.lib.host_dec(0)
        ok, called, _, _ = self._slew(self.ha_ra(160), -60)
        self.assertFalse(ok)
        self.assertEqual(called, 0)

    def test_goto_arrival_is_small_advance(self):
        # A modest slew advances the destination by only the sidereal drift
        # (≈0.03°), never by a 360° (24h LST) jump.
        self.lib.host_limits(-150, 150, -150, 150)
        self.lib.host_ra(0)
        self.lib.host_dec(0)
        ok0, inst = self.reverse(self.ha_ra(0), -60, Axis(0, 0))
        self.assertTrue(ok0)
        ok, _, ra, dec = self._slew(self.ha_ra(0), -60)
        self.assertTrue(ok)
        self.assertAlmostEqual(ra, inst.ra, delta=1.0)
        self.assertAlmostEqual(dec, inst.dec, delta=0.1)

    def test_goto_candidate_just_outside_ra_min(self):
        # Candidate R(t0)=-90.01 is just below ra_min=-90, but its compensated
        # destination -89.939 is inside: it must not be discarded prematurely.
        self.lib.host_limits(-90, 90, 0, 120)
        self.lib.host_ra(0)
        self.lib.host_dec(30)
        ok, called, ra, dec = self._slew(self.ha_ra(-90.01), -60)
        self.assertTrue(ok)
        self.assertEqual(called, 1)
        self.assertTrue(-90 <= ra <= 90, ra)
        self.assertAlmostEqual(dec, 30, delta=0.5)

    def test_goto_destination_within_limits(self):
        # The destination, quantized to steps, stays within the limit steps.
        self.lib.host_limits(-150, 150, -150, 150)
        self.lib.host_ra(140)
        self.lib.host_dec(30)
        ok, _, ra, dec = self._slew(self.ha_ra(149.0), -60)
        self.assertTrue(ok)
        self.assertTrue(self._quant(-150) <= self._quant(ra) <= self._quant(150), ra)
        self.assertTrue(self._quant(-150) <= self._quant(dec) <= self._quant(150), dec)

    def test_goto_quantized_never_exceeds_limit(self):
        # A limit that is not a whole number of steps (30.00002°): the nearest
        # step (30.0000375°) exceeds the limit, so the selector must clamp the
        # executed destination to the interior step (≈29.999925°).
        self.lib.host_limits(-150, 30.00002, -150, 150)
        self.lib.host_ra(0)
        self.lib.host_dec(0)
        ok, _, ra, _ = self._slew(self.ha_ra(30.00002), -60)
        self.assertTrue(ok)
        self.assertLessEqual(ra, 30.00002, ra)
        self.assertLessEqual(self._quant(ra), self._quant(30.00002))

    def test_goto_lst_wrap_continuity(self):
        # Two instants 60s apart straddling the 24h LST wrap must yield a
        # continuous destination (differing by ~sidereal drift, never 360°).
        self.lib.host_limits(-150, 150, -150, 150)
        self.lib.host_ra(0)
        self.lib.host_dec(0)
        jd0 = 2451545.0 + (360.0 - 280.46061837) / 360.98564736629
        t0 = (jd0 - 2440587.5) * 86400.0
        self.lib.host_time(t0 - 60)
        ok0, _, ra0, _ = self._slew(5.0, -60)
        self.lib.host_time(t0 + 60)
        ok1, _, ra1, _ = self._slew(5.0, -60)
        self.assertTrue(ok0 and ok1)
        self.assertLess(abs(ra1 - ra0), 10.0)

    # ── guiding ─────────────────────────────────────────────────────────
    def _guide(self, direction, dec, lat=LAT_SOUTH):
        self.lib.host_lat(lat)
        self.lib.host_dec(dec)
        return self.lib.host_guide(direction)

    def test_guide_signs_south(self):
        # East -> -RA, West -> +RA.
        self.assertTrue(self._guide(2, 30))
        self.assertEqual(C.c_int.in_dll(self.lib, "guide_axis").value, 0)
        self.assertLess(C.c_float.in_dll(self.lib, "guide_offset").value, 0)
        self.assertTrue(self._guide(3, 30))
        self.assertEqual(C.c_int.in_dll(self.lib, "guide_axis").value, 0)
        self.assertGreater(C.c_float.in_dll(self.lib, "guide_offset").value, 0)
        # North on D>0 increases D (+), on D<0 decreases D (-).
        self.assertTrue(self._guide(0, 30))
        self.assertEqual(C.c_int.in_dll(self.lib, "guide_axis").value, 1)
        self.assertGreater(C.c_float.in_dll(self.lib, "guide_offset").value, 0)
        self.assertTrue(self._guide(0, -30))
        self.assertLess(C.c_float.in_dll(self.lib, "guide_offset").value, 0)

    def test_guide_signs_north(self):
        # North on D>0 decreases D (-), on D<0 increases D (+).
        self.assertTrue(self._guide(0, 30, LAT_NORTH))
        self.assertLess(C.c_float.in_dll(self.lib, "guide_offset").value, 0)
        self.assertTrue(self._guide(0, -30, LAT_NORTH))
        self.assertGreater(C.c_float.in_dll(self.lib, "guide_offset").value, 0)

    def test_guide_rejected_at_pole(self):
        # N/S at the pole (D=0) is rejected; E/W is allowed.
        self.assertFalse(self._guide(0, 0))
        self.assertFalse(self._guide(1, 0))
        self.assertTrue(self._guide(2, 0))
        self.assertTrue(self._guide(3, 0))

    def test_guide_rejected_crossing_pole(self):
        # A South pulse from a small positive D whose arc reaches past D=0 is
        # rejected (the signed DEC segment crosses the pole).
        self.lib.host_lat(LAT_SOUTH)
        self.lib.host_dec(0.1)
        self.lib.mount_set_guide_rate_dec(0.5)   # arc = 0.25° > 0.1° to the pole
        try:
            self.assertFalse(self.lib.host_guide(1))   # South -> toward D=0
            self.assertTrue(self.lib.host_guide(0))    # North -> away from D=0
        finally:
            self.lib.mount_set_guide_rate_dec(0.004178074 * 0.5)


if __name__ == "__main__":
    unittest.main()
