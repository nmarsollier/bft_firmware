"""GOT — Pointing at real objects (ACTION goto)."""
import pytest

from conftest import assert_goto_duration, setup_site_time, wait_not_moving, wait_slewing


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_goto_reaches_destination(proto):
    """GOTO from rest reaches the reported destination (duration + arrival)."""
    setup_site_time(proto)
    m = assert_goto_duration(proto, 6.0, -60.0, 6.0)
    assert m["elapsed"] >= m["expected"] * 0.5


def test_goto_to_current_position(proto):
    """GOTO to the current position causes no excursion."""
    setup_site_time(proto)
    st = proto.state()
    proto.action("goto", ra=st["ra"], dec=st["dec"], speed=4)
    wait_not_moving(proto)
    assert proto.state()["state"] != "slewing"


def test_goto_during_goto(proto):
    """A new GOTO during another GOTO leaves a coherent state."""
    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_slewing(proto)
    proto.action("goto", ra=8.0, dec=-45.0, speed=4)
    wait_not_moving(proto)
    assert proto.state()["state"] != "slewing"
