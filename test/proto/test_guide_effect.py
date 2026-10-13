"""GUIDE-effect — a PulseGuide moves the axis by rate × duration."""
import time

import pytest

from conftest import axis_position, setup_site_time, wait_not_moving

GUIDE_RATE = 0.5          # deg/s
DEG_PER_STEP = 1.8 / (64 * 300)


def _set_guide_rate(proto, rate):
    proto.config_set(guide_rate_ra=rate, guide_rate_dec=rate)


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
    _set_guide_rate(proto, GUIDE_RATE)
    before, after = _measure_pulse(proto, "north", 500)
    change_steps = after["dec_steps"] - before["dec_steps"]
    expected_steps = GUIDE_RATE * 500 / 1000.0 / DEG_PER_STEP
    assert abs(abs(change_steps) - expected_steps) <= expected_steps * 0.3 + 4, \
        f"DEC change {change_steps} steps != expected ~{expected_steps:.0f}"


def test_pulse_east_moves_ra_negative(proto):
    """An East RA pulse moves the mechanical RA axis negative."""
    setup_site_time(proto)
    _set_guide_rate(proto, GUIDE_RATE)
    before, after = _measure_pulse(proto, "east", 500)
    assert after["ra_steps"] < before["ra_steps"], "East pulse did not move RA negative"


def test_pulse_flip_reverses_dec(proto):
    """A North pulse moves DEC + on pierEast and − on pierWest."""
    setup_site_time(proto)
    _set_guide_rate(proto, GUIDE_RATE)
    lst = proto.state()["lst"]

    ra_west = (lst - 4.0) % 24.0   # pierEast
    proto.action("goto", ra=ra_west, dec=-60.0, speed=4)
    wait_not_moving(proto)
    b1, a1 = _measure_pulse(proto, "north", 500)
    assert a1["dec_steps"] > b1["dec_steps"], "North should increase dec_axis on pierEast"

    ra_east = (lst + 4.0) % 24.0   # pierWest
    proto.action("goto", ra=ra_east, dec=-60.0, speed=4)
    wait_not_moving(proto)
    b2, a2 = _measure_pulse(proto, "north", 500)
    assert a2["dec_steps"] < b2["dec_steps"], "North should decrease dec_axis on pierWest"


def test_pulse_at_dec_limit_clamped(proto):
    """A pulse toward the DEC axis limit is clamped (does not pass it)."""
    setup_site_time(proto)
    _set_guide_rate(proto, GUIDE_RATE)
    lst = proto.state()["lst"]
    proto.action("goto", ra=lst, dec=60.0, speed=4)   # DEC axis = ±150 (limit)
    wait_not_moving(proto)

    before, after = _measure_pulse(proto, "north", 500)
    assert abs(after["dec_deg"]) <= 150.0 + 0.01, \
        f"DEC axis exceeded the limit: {after['dec_deg']}"
    full_steps = GUIDE_RATE * 0.5 / DEG_PER_STEP
    assert abs(after["dec_steps"] - before["dec_steps"]) < full_steps * 0.5, \
        "pulse at limit was not clamped"


MODES = {0: "sidereal", 1: "lunar", 2: "solar"}


@pytest.mark.parametrize("tracking,rate", [(False, None), (True, 0), (True, 1), (True, 2)])
def test_pulse_tracking_modes(proto, tracking, rate):
    """A DEC pulse works with no tracking and with every tracking rate."""
    setup_site_time(proto)
    proto.action("home")          # start from a known position, away from limits
    wait_not_moving(proto)
    _set_guide_rate(proto, GUIDE_RATE)
    if tracking:
        proto.control(tracking=MODES[rate])
    else:
        proto.control(tracking="none")
    before, after = _measure_pulse(proto, "north", 500)
    change_steps = after["dec_steps"] - before["dec_steps"]
    expected_steps = GUIDE_RATE * 500 / 1000.0 / DEG_PER_STEP
    assert abs(abs(change_steps) - expected_steps) <= expected_steps * 0.3 + 4
