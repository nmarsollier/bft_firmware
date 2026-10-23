"""Fork meridian crossing: continuous pointing without changing orientation."""
import pytest

from conftest import (assert_angular_close, setup_site_time, wait_not_moving,
                      wait_tracking)


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


@pytest.mark.parametrize("tracking", ["none", "sidereal", "lunar", "solar"])
def test_same_object_across_meridian(proto, tracking):
    """GOTO the same object before/after the meridian keeps the orientation."""
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)
    ra = (proto.state()["lst"] + 2.0) % 24.0
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)
    before = proto.state()
    assert -31 < before["ra_axis_deg"] < -29
    assert abs(before["dec_axis_deg"] - 30) < 0.1
    assert before["pier_side"] == "unknown"
    proto.control(tracking=tracking)
    wait_tracking(proto, tracking)

    proto.config_set(utc="2026-09-28T06:00:00Z")
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)
    after = proto.state()
    assert 29 < after["ra_axis_deg"] < 32
    assert abs(after["dec_axis_deg"] - before["dec_axis_deg"]) < 0.1
    assert after["tracking"] == tracking
    assert after["pier_side"] == "unknown"
    assert_angular_close(after["ra"], after["dec"], ra, -60, 0.1)
