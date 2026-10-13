"""GOT-sequence — consecutive gotos, NINA-style corrections, from home."""
import pytest

from conftest import (assert_angular_close, assert_goto_duration, axis_position,
                      setup_site_time, wait_not_moving)

SPEED_DPS = 6.0
TOL = 0.1


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_consecutive_gotos(proto):
    """A chain of gotos each reaches its target."""
    setup_site_time(proto)
    for ra, dec in [(6.0, -60.0), (8.0, -45.0), (10.0, -70.0), (6.0, -55.0)]:
        m = assert_goto_duration(proto, ra, dec, SPEED_DPS)
        assert_angular_close(m["final_ra"], m["final_dec"], ra, dec, TOL, f"goto {ra}h/{dec}°")


def test_nina_style_goto_then_correct(proto):
    """N.I.N.A. pattern: a large goto followed by a small correction."""
    setup_site_time(proto)
    assert_goto_duration(proto, 6.0, -60.0, SPEED_DPS)
    r = assert_goto_duration(proto, 6.0, -62.0, SPEED_DPS)
    assert r["ra_dist"] + r["dec_dist"] < 5.0, "correction was not small"


def test_goto_from_home(proto):
    """A goto started from home begins at axis (0,0) and reaches its target."""
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)
    start = axis_position(proto)
    assert abs(start["ra_deg"]) < 1.0 and abs(start["dec_deg"]) < 1.0
    m = assert_goto_duration(proto, 6.0, -60.0, SPEED_DPS)
    assert_angular_close(m["final_ra"], m["final_dec"], 6.0, -60.0, TOL, "from home")


def test_nina_correct_at_dec_limit(proto):
    """NINA-style correction near the DEC axis limit stays valid."""
    setup_site_time(proto)
    lst = proto.state()["lst"]
    assert_goto_duration(proto, lst, 58.0, SPEED_DPS)
    assert_goto_duration(proto, lst, 55.0, SPEED_DPS)


def test_nina_correct_across_meridian(proto):
    """A correction crossing the meridian flips the pier side."""
    setup_site_time(proto)
    lst = proto.state()["lst"]

    def pier():
        return 0 if proto.state()["pier_side"] == "east" else 1

    ra_w = (lst - 1.0) % 24.0   # just west → pierEast
    assert_goto_duration(proto, ra_w, -60.0, SPEED_DPS)
    assert pier() == 0
    ra_e = (lst + 1.0) % 24.0   # just east → pierWest (flip)
    assert_goto_duration(proto, ra_e, -60.0, SPEED_DPS)
    assert pier() == 1
