"""GOTO across the meridian preserves the fork orientation."""
import pytest

from conftest import assert_angular_close, assert_goto_duration, setup_site_time

SPEED_DPS = 4.0


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_meridian_crossing_uses_min_travel(proto):
    """Crossing the meridian picks the reachable minimum-travel position.  With
    RA ±120 the flipped equivalent (R=120 at HA=-4h) is out of range, so the
    mount crosses via the unflipped (R=-60, D=+30) instead of the negative side."""
    setup_site_time(proto)
    lst = proto.state()["lst"]

    ra_west = (lst - 4.0) % 24.0   # HA = +4h -> unflipped (R=+60, D=+30)
    assert_goto_duration(proto, ra_west, -60.0, SPEED_DPS)
    assert abs(proto.state()["dec_axis_deg"] - 30) < 0.1
    assert 59 < proto.state()["ra_axis_deg"] < 61

    ra_east = (lst + 4.0) % 24.0   # HA = -4h -> flipped (R=120,D=-30) out of RA range
    assert_goto_duration(proto, ra_east, -60.0, SPEED_DPS)
    assert abs(proto.state()["dec_axis_deg"] - 30) < 0.1
    assert -61 < proto.state()["ra_axis_deg"] < -59


def test_goto_near_meridian(proto):
    """A target on the meridian (HA≈0) is reached; the side is not forced."""
    setup_site_time(proto)
    lst = proto.state()["lst"]
    ra = lst % 24.0
    m = assert_goto_duration(proto, ra, -60.0, SPEED_DPS)
    assert_angular_close(m["final_ra"], m["final_dec"], ra, -60.0, 0.1, "near-meridian goto")
