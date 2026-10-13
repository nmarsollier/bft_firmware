"""SESSION — a long mixed session stays coherent (tracking + guide + goto + park)."""
import pytest

from conftest import setup_site_time, wait_not_moving, wait_tracking


@pytest.fixture(autouse=True)
def _settle(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")
    try:
        proto.action("limits", param="clear_limits")
    except Exception:
        pass


def test_prolonged_mixed_session(proto):
    """Tracking + guide + small goto + cancel + park, staying coherent."""
    setup_site_time(proto)

    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")

    proto.config_set(guide_rate_dec=0.5)
    proto.action("guide", direction="north", duration_ms=300)

    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_not_moving(proto)
    assert proto.state()["tracking"] == "sidereal", "tracking lost after goto"

    proto.action("stop")
    proto.action("park")
    assert proto.state()["at_park"] is True
    proto.action("unpark")
    assert proto.state()["at_park"] is False

    proto.action("home")
    wait_not_moving(proto)


def test_tracking_stops_at_test_limit(proto):
    """Tracking toward an interior test limit stops there, without passing it."""
    import time
    from conftest import axis_position

    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)
    proto.control(manual_ra_dps=2.0)
    time.sleep(3.0)
    proto.control(manual_ra_dps=0.0)
    wait_not_moving(proto)
    proto.action("limits", param="set_ra_right")
    limit = axis_position(proto)["ra_deg"]

    proto.control(manual_ra_dps=-2.0)
    time.sleep(3.0)
    proto.control(manual_ra_dps=0.0)
    wait_not_moving(proto)

    proto.control(tracking="sidereal")
    time.sleep(3.0)
    pos = axis_position(proto)
    assert pos["ra_deg"] <= limit + 0.1, \
        f"tracking passed the test limit {limit:.2f}° (now {pos['ra_deg']:.2f}°)"
    proto.control(tracking="none")
