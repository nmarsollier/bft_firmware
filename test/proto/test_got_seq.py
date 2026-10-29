"""GOT-sequence — consecutive gotos, NINA-style corrections, from home."""
import pytest

from conftest import (assert_angular_close, assert_goto_duration, axis_position,
                      setup_site_time, wait_not_moving)

SPEED_DPS = 4.0
TOL = 0.1


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_consecutive_gotos(proto):
    """A chain of gotos each reaches its target."""
    setup_site_time(proto)
    for ra, dec in [(6.0, -60.0), (5.0, -45.0), (7.0, -70.0), (6.0, -55.0)]:
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
    """A small correction crossing the meridian stays small on both axes."""
    setup_site_time(proto)
    lst = proto.state()["lst"]
    assert_goto_duration(proto, (lst - 0.05) % 24, -60, SPEED_DPS)
    result = assert_goto_duration(proto, (lst + 0.05) % 24, -60, SPEED_DPS)
    assert result["ra_dist"] < 2.0
    assert result["dec_dist"] < 0.1
    assert abs(result["end_axis"]["dec_deg"] - 30) < 0.1
