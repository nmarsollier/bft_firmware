"""GUIDE-effect — a PulseGuide moves the axis by rate × duration."""
import time

import pytest

from client import DeviceError
from conftest import axis_position, setup_site_time, wait_not_moving

GUIDE_RATE = 0.5          # deg/s
DEG_PER_STEP = 1.8 / (32 * 500)   # 32 microsteps × 500:1 reduction

# The mount's default guide rate (0.5 × sidereal).  GUIDE_RATE above is a
# manual-slew speed (239 × sidereal) chosen only to make coarse magnitude/direction
# assertions easy; guiding actually runs at this small rate.
SIDEREAL_DPS = 0.004178074
DEFAULT_GUIDE_RATE = SIDEREAL_DPS * 0.5   # ≈ 0.002089 deg/s


def _set_guide_rate(proto, rate):
    proto.config_set(guide_rate_ra=rate, guide_rate_dec=rate)


def _move_dec(proto, degrees):
    proto.action("move", axis="dec", degrees=degrees, speed=4)
    wait_not_moving(proto)


def _measure_pulse(proto, direction, duration_ms):
    before = axis_position(proto)
    proto.action("guide", direction=direction, duration_ms=duration_ms)
    time.sleep(duration_ms / 1000.0 + 0.5)   # let the pulse finish
    after = axis_position(proto)
    return before, after


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_pulse_effect_magnitude(proto):
    """A DEC pulse moves the axis by rate × duration."""
    _move_dec(proto, 30.0)          # away from the pole (D=30)
    _set_guide_rate(proto, GUIDE_RATE)
    before, after = _measure_pulse(proto, "north", 500)
    change_steps = after["dec_steps"] - before["dec_steps"]
    expected_steps = GUIDE_RATE * 500 / 1000.0 / DEG_PER_STEP
    assert abs(abs(change_steps) - expected_steps) <= expected_steps * 0.3 + 4, \
        f"DEC change {change_steps} steps != expected ~{expected_steps:.0f}"


def test_pulse_default_guide_rate_dec(proto):
    """A DEC pulse at the default 0.5×sidereal rate moves by rate × duration.

    The coarse magnitude test above uses 0.5 °/s; this exercises the actual
    guiding rate so a rate that only behaves at manual speeds is caught.
    """
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)
    _move_dec(proto, 30.0)          # away from the pole (D=30)
    _set_guide_rate(proto, DEFAULT_GUIDE_RATE)

    # 5 s at ~18.6 steps/s ≈ 93 steps — enough to measure, still a guide speed.
    before, after = _measure_pulse(proto, "north", 5000)
    change_steps = after["dec_steps"] - before["dec_steps"]
    expected_steps = DEFAULT_GUIDE_RATE * 5.0 / DEG_PER_STEP
    assert abs(abs(change_steps) - expected_steps) <= expected_steps * 0.3 + 4, \
        f"DEC change {change_steps} steps != expected ~{expected_steps:.0f}"


def test_pulse_default_guide_rate_ra(proto):
    """An RA pulse at the default 0.5×sidereal rate moves by rate × duration."""
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)
    _set_guide_rate(proto, DEFAULT_GUIDE_RATE)

    before, after = _measure_pulse(proto, "east", 5000)
    change_steps = after["ra_steps"] - before["ra_steps"]
    expected_steps = DEFAULT_GUIDE_RATE * 5.0 / DEG_PER_STEP
    assert abs(abs(change_steps) - expected_steps) <= expected_steps * 0.3 + 4, \
        f"RA change {change_steps} steps != expected ~{expected_steps:.0f}"


def test_pulse_east_moves_ra_negative(proto):
    """An East RA pulse moves the mechanical RA axis negative."""
    setup_site_time(proto)
    _set_guide_rate(proto, GUIDE_RATE)
    before, after = _measure_pulse(proto, "east", 500)
    assert after["ra_steps"] < before["ra_steps"], "East pulse did not move RA negative"


def test_pulse_north_follows_dec_side(proto):
    """North increases celestial DEC: +D on the positive side, -D on the
    negative side. The mechanical sign follows the current geometry, not any
    remembered orientation, so it flips when the minimum-travel GOTO lands on
    the other side of the pole."""
    setup_site_time(proto)
    _set_guide_rate(proto, GUIDE_RATE)
    lst = proto.state()["lst"]

    ra_west = (lst - 4.0) % 24.0   # west of meridian -> positive side (D=+30)
    proto.action("goto", ra=ra_west, dec=-60.0, speed=4)
    wait_not_moving(proto)
    b1, a1 = _measure_pulse(proto, "north", 500)
    assert a1["dec_steps"] > b1["dec_steps"], \
        "North should increase dec_axis on the positive side"

    ra_east = (lst + 6.0) % 24.0   # HA=-6h east -> flipped equivalent (D=-30) still in RA range
    proto.action("goto", ra=ra_east, dec=-60.0, speed=4)
    wait_not_moving(proto)
    b2, a2 = _measure_pulse(proto, "north", 500)
    assert a2["dec_steps"] < b2["dec_steps"], \
        "North should decrease dec_axis on the negative side"


def test_pulse_at_dec_limit_rejected(proto):
    """A pulse that would exceed the DEC axis limit is rejected upfront, not
    silently clamped into a partial (or zero) move."""
    setup_site_time(proto)
    _set_guide_rate(proto, GUIDE_RATE)
    lst = proto.state()["lst"]
    proto.action("goto", ra=lst, dec=60.0, speed=4)   # DEC axis = +150 (limit)
    wait_not_moving(proto)

    with pytest.raises(DeviceError):
        proto.action("guide", direction="north", duration_ms=500)


MODES = {0: "sidereal", 1: "lunar", 2: "solar"}


@pytest.mark.parametrize("tracking,rate", [(False, None), (True, 0), (True, 1), (True, 2)])
def test_pulse_tracking_modes(proto, tracking, rate):
    """A DEC pulse works with no tracking and with every tracking rate."""
    setup_site_time(proto)
    proto.action("home")          # start from a known position, away from limits
    wait_not_moving(proto)
    _move_dec(proto, 30.0)        # away from the pole
    _set_guide_rate(proto, GUIDE_RATE)
    if tracking:
        proto.control(tracking=MODES[rate])
    else:
        proto.control(tracking="none")
    before, after = _measure_pulse(proto, "north", 500)
    change_steps = after["dec_steps"] - before["dec_steps"]
    expected_steps = GUIDE_RATE * 500 / 1000.0 / DEG_PER_STEP
    assert abs(abs(change_steps) - expected_steps) <= expected_steps * 0.3 + 4
