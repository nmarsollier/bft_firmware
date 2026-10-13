"""MER — Pier side and meridian flip (STATE pier_side + ACTION goto)."""
import pytest

from conftest import setup_site_time, wait_not_moving, wait_tracking


def _pier_num(proto):
    return 0 if proto.state()["pier_side"] == "east" else 1


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_side_of_pier_report(proto):
    """STATE.pier_side returns a valid value (east=0, west=1)."""
    assert _pier_num(proto) in (0, 1)


def test_meridian_flip_same_object(proto):
    """GOTO to the same object after crossing the meridian flips the pier."""
    setup_site_time(proto)
    lst = proto.state()["lst"]

    ra = (lst + 2.0) % 24.0   # HA = -2h (east) → pierWest
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)
    assert _pier_num(proto) == 1, f"HA=-2h should be pierWest, got {_pier_num(proto)}"

    proto.config_set(utc="2026-09-28T06:00:00Z")   # advance 4h → HA = +2h
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)
    assert _pier_num(proto) == 0, f"HA=+2h should be pierEast (flip), got {_pier_num(proto)}"


def test_meridian_flip_preserves_tracking(proto):
    """A flip GOTO with tracking active resumes tracking afterwards."""
    setup_site_time(proto)
    lst = proto.state()["lst"]
    ra = (lst + 2.0) % 24.0   # HA = -2h
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)

    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")

    proto.config_set(utc="2026-09-28T06:00:00Z")
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)
    assert proto.state()["tracking"] == "sidereal", "flip GOTO did not resume tracking"
