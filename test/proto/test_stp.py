"""STP — Stop and cancellation (ACTION stop)."""
import time

import pytest

from conftest import setup_site_time, wait_not_moving, wait_slewing, wait_tracking


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_stop_stops_slew(proto):
    """ACTION stop halts an in-progress maneuver."""
    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_slewing(proto)
    proto.action("stop")
    wait_not_moving(proto)
    assert proto.state()["state"] != "slewing"


def test_stop_cancels_queued_goto(proto):
    """A goto queued behind an active one is cancelled by STOP and never runs."""
    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_slewing(proto)
    proto.action("goto", ra=7.0, dec=-50.0, speed=4)   # queued
    proto.action("stop")
    wait_not_moving(proto)
    time.sleep(5.0)
    assert proto.state()["state"] != "slewing", "cancelled goto re-appeared"


def test_repeat_stop_when_quiet(proto):
    """Repeating stop while quiet is safe and idempotent."""
    for _ in range(3):
        proto.action("stop")
    assert proto.state()["state"] != "slewing"


def test_move_again_after_stop(proto):
    """After a stop the mount can move again."""
    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_slewing(proto)
    proto.action("stop")
    wait_not_moving(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_slewing(proto)
    proto.action("stop")
    wait_not_moving(proto)


def test_stop_clears_tracking(proto):
    """ACTION stop (scope all) clears an active tracking."""
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    proto.action("stop")
    assert proto.state()["tracking"] == "none"


def test_stop_manual_preserves_tracking(proto):
    """Stopping a manual move (scope manual) restores the paused tracking."""
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    proto.control(manual_ra_dps=1.0)          # pauses tracking
    proto.action("stop", scope="manual")
    # Tracking is restored asynchronously (the TRACK command is queued), so
    # wait for it rather than asserting on the immediate read.
    wait_tracking(proto, "sidereal")


def test_stop_then_park(proto):
    """Stop followed by park leaves the mount parked."""
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    proto.action("stop")
    proto.action("park")
    assert proto.state()["at_park"] is True
    proto.action("unpark")
