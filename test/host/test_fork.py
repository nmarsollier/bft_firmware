"""Execute production C conversion and guide mapping without touching hardware."""
import ctypes as C
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NOW = 1780000000


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
            str(ROOT / "main/mount/mount_pulse_guide.c"),
            str(ROOT / "test/host/mount_stubs.c"), "-lm", "-o", str(lib),
        ], check=True)
        cls.lib = C.CDLL(str(lib))
        cls.lib.host_reset.argtypes = [C.c_long]
        cls.lib.host_limits.argtypes = [C.c_float] * 4
        cls.lib.host_dec.argtypes = [C.c_float]
        cls.lib.host_set_orientation.argtypes = [C.c_bool]
        cls.lib.axis_to_equatorial.argtypes = [Axis]
        cls.lib.axis_to_equatorial.restype = Eq
        cls.lib.equatorial_to_axis.argtypes = [Eq, Axis, C.c_long, C.POINTER(Axis)]
        cls.lib.equatorial_to_axis.restype = C.c_bool
        cls.lib.host_guide.argtypes = [C.c_int]
        cls.lib.host_guide.restype = C.c_bool

    def setUp(self):
        self.lib.host_reset(NOW)
        # Independent sidereal angle at the fixed UTC instant (USNO formula).
        jd = NOW / 86400 + 2440587.5
        t = (jd - 2451545) / 36525
        self.lst = (280.46061837 + 360.98564736629 * (jd - 2451545)
                    + .000387933 * t*t - t*t*t / 38710000) % 360 / 15

    def convert(self, ha, dec=-60, current=None):
        out = Axis(999, 999)
        ok = self.lib.equatorial_to_axis(Eq((self.lst - ha / 15) % 24, dec),
                                         current or Axis(0, 0), NOW, C.byref(out))
        return ok, out

    def test_meridian_continuity(self):
        previous = Axis(-1, 30)
        for ha in [-.1, -.01, 0, .01, .1]:
            ok, axis = self.convert(ha, current=previous)
            self.assertTrue(ok)
            self.assertAlmostEqual(axis.ra, ha, delta=.001)
            self.assertAlmostEqual(axis.dec, 30, delta=.001)
            self.assertLess(abs(axis.ra - previous.ra), 2)
            previous = axis

    def test_both_orientations_round_trip(self):
        for sign in [1, -1]:
            self.lib.host_set_orientation(sign < 0)
            for ra in [-140, -60, 0, 60, 140]:
                for dec in [0.01, 30, 90, 149]:
                    original = Axis(ra, sign * dec)
                    eq = self.lib.axis_to_equatorial(original)
                    out = Axis()
                    self.assertTrue(self.lib.equatorial_to_axis(eq, original, NOW, C.byref(out)))
                    self.assertAlmostEqual(out.ra, ra, delta=.001)
                    self.assertAlmostEqual(out.dec, sign * dec, delta=.001)

    def test_orientations_point_at_same_sky(self):
        """The two physical orientations map to the same sky position.

        Independent of the round-trip: the positive branch (ra_axis=ra,
        dec_axis=+dec) and the negative branch (ra_axis=ra+180, dec_axis=-dec)
        must resolve to the same RA/DEC.  It does not reuse the same formula in
        both directions, so it catches a sign/offset error that a self-consistent
        round-trip would miss.
        """
        for ra in [-140, -60, 0, 60, 140]:
            for dec in [0.01, 30, 90, 149]:
                self.lib.host_set_orientation(False)
                pos = self.lib.axis_to_equatorial(Axis(ra, dec))
                self.lib.host_set_orientation(True)
                neg = self.lib.axis_to_equatorial(Axis(ra + 180, -dec))
                self.assertAlmostEqual(pos.dec, neg.dec, delta=.001)
                self.assertAlmostEqual(pos.ra, neg.ra, delta=.001)

    def test_no_orientation_switch_to_bypass_ra_limit(self):
        # HA=160 would fit at RA=-20, DEC=-30 only by flipping.
        ok, out = self.convert(160)
        self.assertFalse(ok)
        self.assertEqual(out.ra, 999)

    def test_no_orientation_switch_to_bypass_dec_limit(self):
        self.lib.host_limits(-150, 150, -150, 20)
        self.assertFalse(self.convert(60)[0])

    def test_runtime_limits(self):
        self.lib.host_limits(-10, 10, -40, 40)
        self.assertTrue(self.convert(9)[0])
        self.assertFalse(self.convert(11)[0])
        self.assertFalse(self.convert(-11)[0])
        self.assertFalse(self.convert(0, dec=-49)[0])

    def test_pole_keeps_ra_and_checks_limits(self):
        ok, axis = self.convert(-100, dec=-90, current=Axis(75, 30))
        self.assertTrue(ok)
        self.assertEqual(axis.ra, 75)
        self.assertEqual(axis.dec, 0)
        self.lib.host_limits(-10, 10, -150, 150)
        self.assertFalse(self.convert(0, dec=-90, current=Axis(75, 30))[0])

    def test_no_full_revolution_to_bypass_limit(self):
        self.lib.host_limits(200, 400, -150, 150)
        self.assertFalse(self.convert(-60, current=Axis(250, 30))[0])

    def test_reject_invalid_coordinates(self):
        for eq in [Eq(math.nan, 0), Eq(math.inf, 0), Eq(0, math.nan),
                   Eq(0, math.inf), Eq(0, -91), Eq(0, 91)]:
            self.assertFalse(self.lib.equatorial_to_axis(eq, Axis(), NOW, C.byref(Axis())))

    def test_guiding_follows_physical_orientation(self):
        for dec in [30, -30]:
            self.lib.host_dec(dec)
            for direction, sign in [(0, 1), (1, -1)]:
                self.assertTrue(self.lib.host_guide(direction))
                self.assertEqual(C.c_int.in_dll(self.lib, "guide_axis").value, 1)
                offset = C.c_float.in_dll(self.lib, "guide_offset").value
                self.assertGreater(offset * sign * dec, 0)
            for direction, sign in [(2, -1), (3, 1)]:
                self.assertTrue(self.lib.host_guide(direction))
                self.assertEqual(C.c_int.in_dll(self.lib, "guide_axis").value, 0)
                self.assertGreater(C.c_float.in_dll(self.lib, "guide_offset").value * sign, 0)

    def test_pole_exit_preserves_negative_orientation(self):
        """Negative orientation -> pole -> original object, no 180° swing."""
        self.lib.host_set_orientation(True)   # negative
        self.lib.host_dec(-30)

        original = Axis(60, -30)
        eq = self.lib.axis_to_equatorial(original)

        # GOTO the south pole: DEC -> 0, RA preserved.
        at_pole = Axis()
        self.assertTrue(self.lib.equatorial_to_axis(
            Eq(eq.ra, -90), original, NOW, C.byref(at_pole)))
        self.assertAlmostEqual(at_pole.ra, 60, delta=.001)
        self.assertAlmostEqual(at_pole.dec, 0, delta=.001)

        # GOTO back from the pole recovers ra=60, dec=-30 (not ra=-120, dec=+30).
        back = Axis()
        self.assertTrue(self.lib.equatorial_to_axis(
            eq, Axis(60, 0), NOW, C.byref(back)))
        self.assertAlmostEqual(back.ra, 60, delta=.001)
        self.assertAlmostEqual(back.dec, -30, delta=.001)

    def test_pole_exit_preserves_positive_orientation(self):
        """The same pole round-trip from positive orientation."""
        self.lib.host_set_orientation(False)  # positive
        self.lib.host_dec(30)

        original = Axis(60, 30)
        eq = self.lib.axis_to_equatorial(original)

        at_pole = Axis()
        self.assertTrue(self.lib.equatorial_to_axis(
            Eq(eq.ra, -90), original, NOW, C.byref(at_pole)))
        self.assertAlmostEqual(at_pole.dec, 0, delta=.001)

        back = Axis()
        self.assertTrue(self.lib.equatorial_to_axis(
            eq, Axis(60, 0), NOW, C.byref(back)))
        self.assertAlmostEqual(back.ra, 60, delta=.001)
        self.assertAlmostEqual(back.dec, 30, delta=.001)

    def test_pole_queries_do_not_modify_orientation(self):
        """Repeated queries at the pole keep the remembered negative orientation."""
        self.lib.host_set_orientation(True)   # negative
        at_pole = Axis(60, 0)
        for _ in range(3):
            eq = self.lib.axis_to_equatorial(at_pole)
            out = Axis()
            self.assertTrue(self.lib.equatorial_to_axis(
                eq, at_pole, NOW, C.byref(out)))
            self.assertAlmostEqual(out.ra, 60, delta=.001)
            self.assertAlmostEqual(out.dec, 0, delta=.001)
        # A subsequent non-pole GOTO still uses the negative branch.
        ok, axis = self.convert(-60, dec=-60, current=Axis(60, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(axis.dec, -30, delta=.001)

    def test_manual_cross_updates_orientation(self):
        """A manual move crossing the pole re-derives the orientation."""
        # Positive orientation at dec=+30.
        self.lib.host_dec(30)
        ok, axis = self.convert(-60, dec=-60, current=Axis(0, 30))
        self.assertTrue(ok)
        self.assertAlmostEqual(axis.dec, 30, delta=.001)   # positive branch

        # Manual move crosses the pole to dec=-30: orientation becomes negative.
        self.lib.host_dec(-30)
        ok, axis = self.convert(-60, dec=-60, current=Axis(0, -30))
        self.assertTrue(ok)
        self.assertAlmostEqual(axis.dec, -30, delta=.001)  # negative branch

    def test_home_resets_positive_orientation(self):
        """An explicit home re-establishes the positive orientation."""
        self.lib.host_set_orientation(True)   # negative
        self.lib.host_dec(-30)
        # Home (motors_home / set_current_as_home) resets to positive.
        self.lib.host_set_orientation(False)
        self.lib.host_dec(0)
        ok, axis = self.convert(-60, dec=-60, current=Axis(0, 0))
        self.assertTrue(ok)
        self.assertAlmostEqual(axis.dec, 30, delta=.001)   # positive branch

    def test_rejected_goto_preserves_orientation(self):
        """A rejected GOTO does not modify the remembered orientation."""
        self.lib.host_set_orientation(True)   # negative
        self.lib.host_dec(-30)
        # Target out of the negative branch's limits.
        ok, out = self.convert(0, dec=-60, current=Axis(0, -30))
        self.assertFalse(ok)
        self.assertEqual(out.ra, 999)
        # Orientation unchanged: a reachable target still uses the negative branch.
        ok, axis = self.convert(-60, dec=-60, current=Axis(0, -30))
        self.assertTrue(ok)
        self.assertAlmostEqual(axis.dec, -30, delta=.001)


if __name__ == "__main__":
    unittest.main()
