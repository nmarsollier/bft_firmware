"""GOT-flip — a goto across the meridian flips the pier side."""
import pytest

from conftest import assert_angular_close, assert_goto_duration, setup_site_time

SPEED_DPS = 6.0


def _pier(proto):
    return 0 if proto.state()["pier_side"] == "east" else 1


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_meridian_flip(proto):
    """West-of-meridian → pierEast; east-of-meridian → pierWest (flip)."""
    setup_site_time(proto)
    lst = proto.state()["lst"]

    ra_west = (lst - 4.0) % 24.0   # HA = +4h → pierEast
    assert_goto_duration(proto, ra_west, -60.0, SPEED_DPS)
    assert _pier(proto) == 0, f"HA=+4h should be pierEast, got {_pier(proto)}"

    ra_east = (lst + 4.0) % 24.0   # HA = -4h → pierWest (flip)
    assert_goto_duration(proto, ra_east, -60.0, SPEED_DPS)
    assert _pier(proto) == 1, f"HA=-4h should be pierWest (flip), got {_pier(proto)}"


def test_goto_near_meridian(proto):
    """A target on the meridian (HA≈0) is reached; the side is not forced."""
    setup_site_time(proto)
    lst = proto.state()["lst"]
    ra = lst % 24.0
    m = assert_goto_duration(proto, ra, -60.0, SPEED_DPS)
    assert_angular_close(m["final_ra"], m["final_dec"], ra, -60.0, 0.1, "near-meridian goto")
