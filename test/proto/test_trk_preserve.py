"""TRK-preserve — no axis movement ever loses the active tracking mode.

Guards the invariant that every motion entry point which can run while
tracking is active leaves the tracking mode unchanged once the motion ends:

  * relative move       (action move)
  * continuous MoveAxis (control manual_ra_dps / manual_dec_dps, single + both)
  * PulseGuide          (action guide)

GOTO is covered by test_got_tracking.py.  Park and home are deliberate
tracking-stopping operations and are covered by test_prk.py / test_ref.py.
"""
import time

import pytest

from conftest import setup_site_time, wait_not_moving, wait_slewing, wait_tracking

MODES = ["sidereal", "lunar", "solar"]
DIRS = ["north", "south", "east", "west"]


def _home(proto):
    proto.action("home")
    wait_not_moving(proto)


def _tracking_on(proto, mode):
    proto.control(tracking=mode)
    wait_tracking(proto, mode)


def _assert_tracking(proto, mode):
    got = proto.state()["tracking"]
    assert got == mode, f"tracking lost after motion: expected {mode}, got {got}"


# ── relative move (action move) ────────────────────────────────────

@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("axis", ["ra", "dec"])
def test_relative_move_preserves_tracking(proto, axis, mode):
    _home(proto)
    _tracking_on(proto, mode)
    proto.action("move", axis=axis, degrees=5.0, speed=4)
    wait_not_moving(proto)
    _assert_tracking(proto, mode)


# ── continuous MoveAxis (control manual_*_dps) ─────────────────────

@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("field", ["manual_ra_dps", "manual_dec_dps"])
def test_move_axis_single_preserves_tracking(proto, field, mode):
    _home(proto)
    _tracking_on(proto, mode)
    proto.control(**{field: 0.5})
    wait_slewing(proto)
    proto.control(**{field: 0.0})
    wait_tracking(proto, mode)
    _assert_tracking(proto, mode)


@pytest.mark.parametrize("mode", MODES)
def test_move_axis_both_preserves_tracking(proto, mode):
    _home(proto)
    _tracking_on(proto, mode)
    proto.control(manual_ra_dps=0.5, manual_dec_dps=0.5)
    wait_slewing(proto)
    proto.control(manual_ra_dps=0.0, manual_dec_dps=0.0)
    wait_tracking(proto, mode)
    _assert_tracking(proto, mode)


# ── PulseGuide (action guide) ──────────────────────────────────────

@pytest.mark.parametrize("direction", DIRS)
def test_pulse_guide_preserves_tracking(proto, direction):
    setup_site_time(proto)
    _home(proto)
    # N/S needs DEC away from the celestial pole; E/W is a pure RA rotation
    # and works even at the pole.
    if direction in ("north", "south"):
        proto.action("move", axis="dec", degrees=30.0, speed=4)
        wait_not_moving(proto)
    _tracking_on(proto, "sidereal")
    proto.action("guide", direction=direction, duration_ms=300)
    time.sleep(0.8)   # let the 300 ms pulse finish
    assert proto.state()["guiding"] is False
    _assert_tracking(proto, "sidereal")


# ── redundant / spurious stop ──────────────────────────────────────

def test_spurious_stop_does_not_drop_tracking(proto):
    """A full manual stop with no move in flight must not stop tracking."""
    _home(proto)
    _tracking_on(proto, "sidereal")
    proto.control(manual_ra_dps=0.0, manual_dec_dps=0.0)
    _assert_tracking(proto, "sidereal")


def test_spurious_single_axis_release_preserves_guide(proto):
    """A redundant single-axis manual release must not disturb tracking or a
    PulseGuide in flight (no manual move is active)."""
    setup_site_time(proto)
    _home(proto)
    proto.action("move", axis="dec", degrees=30.0, speed=4)
    wait_not_moving(proto)
    _tracking_on(proto, "sidereal")

    proto.action("guide", direction="north", duration_ms=500)
    proto.control(manual_ra_dps=0.0)   # no manual RA move active

    time.sleep(0.8)
    assert proto.state()["guiding"] is False, "pulse was disturbed"
    _assert_tracking(proto, "sidereal")
