"""TRK — Tracking (CONTROL family)."""
import time

import pytest

from conftest import axis_position, wait_tracking

# Tracking mode mapping: 0=sidereal, 1=lunar, 2=solar (deg/s)
MODES = {0: "sidereal", 1: "lunar", 2: "solar"}
RATES = {0: 0.004178074, 1: 0.004025576, 2: 0.004166667}


def _ra_wrap_diff(a, b):
    d = abs(a - b)
    return min(d, 24.0 - d)


@pytest.fixture(autouse=True)
def _no_tracking(proto):
    yield
    proto.control(tracking="none")


def test_disable_tracking(proto):
    """tracking=none disables tracking."""
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    proto.control(tracking="none")
    wait_tracking(proto, "none")
    assert proto.state()["tracking"] == "none"


def test_reactivate_tracking(proto):
    """Reactivating after a pause keeps the selection."""
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    proto.control(tracking="none")
    wait_tracking(proto, "none")
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    assert proto.state()["tracking"] == "sidereal"


@pytest.mark.parametrize("rate", [0, 1, 2])
def test_change_tracking_rate(proto, rate):
    """Switch between sidereal (0), lunar (1) and solar (2)."""
    proto.control(tracking=MODES[rate])
    wait_tracking(proto, MODES[rate])
    assert proto.state()["tracking"] == MODES[rate]


def test_tracking_order_idempotent(proto):
    """Repeating the same order accumulates no errors."""
    for _ in range(5):
        proto.control(tracking="sidereal")
        assert proto.state()["tracking"] == "sidereal"
    proto.control(tracking="none")
    wait_tracking(proto, "none")


def test_tracking_keeps_ra_stable(proto):
    """Tracking compensates sidereal drift: RA stays put, unlike at rest."""
    proto.control(tracking="none")
    wait_tracking(proto, "none")
    ra0 = proto.state()["ra"]
    time.sleep(3.0)
    ra1 = proto.state()["ra"]
    drift_off = _ra_wrap_diff(ra0, ra1)

    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    ra2 = proto.state()["ra"]
    time.sleep(3.0)
    ra3 = proto.state()["ra"]
    drift_on = _ra_wrap_diff(ra2, ra3)

    assert drift_off > 0.0003, f"baseline drift too small: {drift_off:.6f} h"
    assert drift_on < drift_off * 0.25, \
        f"tracking did not hold RA: off={drift_off:.6f} on={drift_on:.6f}"


def test_tracking_prolonged_with_queries(proto):
    """Sustained tracking with interleaved queries does not degrade state."""
    proto.control(tracking="sidereal")
    for _ in range(10):
        assert proto.state()["tracking"] == "sidereal"
        assert proto.state()["state"] in ("tracking", "ready")
        time.sleep(0.5)
    proto.control(tracking="none")


@pytest.mark.parametrize("rate", [0, 1, 2])
def test_tracking_rate_coherence(proto, rate):
    """Tracking moves the RA axis at the mode's rate (deg/s)."""
    proto.control(tracking="sidereal")
    proto.control(tracking=MODES[rate])
    wait_tracking(proto, MODES[rate])

    t0 = axis_position(proto)
    time.sleep(15.0)
    t1 = axis_position(proto)

    ra_change = t1["ra_deg"] - t0["ra_deg"]
    expected = RATES[rate] * 15.0
    assert expected * 0.97 <= ra_change <= expected * 1.03, \
        f"rate {rate}: RA moved {ra_change:.6f}° in 15s, expected ~{expected:.6f}°"


def test_tracking_stops_at_limit(proto):
    """Tracking stops when the RA axis reaches its limit."""
    from conftest import wait_not_moving

    proto.action("home")
    wait_not_moving(proto)

    # move RA to the positive limit (+100°)
    proto.control(manual_ra_dps=6.0)
    wait_not_moving(proto, timeout=40)
    pos = axis_position(proto)
    assert pos["ra_deg"] > 99.0, f"RA not near the limit: {pos['ra_deg']}"

    proto.control(tracking="sidereal")
    time.sleep(2.0)
    assert proto.state()["tracking"] == "none", "tracking did not stop at the limit"
    pos2 = axis_position(proto)
    assert pos2["ra_deg"] <= 100.0 + 0.1, f"RA passed the limit: {pos2['ra_deg']}"
