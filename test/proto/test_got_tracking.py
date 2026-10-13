"""GOT-tracking — a goto preserves (or leaves off) the tracking state."""
import pytest

from conftest import assert_goto_duration, setup_site_time, wait_tracking

MODES = {0: "sidereal", 1: "lunar", 2: "solar"}


@pytest.fixture(autouse=True)
def _no_tracking(proto):
    yield
    proto.control(tracking="none")


def test_goto_preserves_tracking(proto):
    """A goto issued while tracking is active resumes tracking afterwards."""
    setup_site_time(proto)
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    assert_goto_duration(proto, 6.0, -60.0, 6.0)
    assert proto.state()["tracking"] == "sidereal", "goto did not preserve tracking"


def test_goto_without_tracking_stays_off(proto):
    """A goto with tracking off leaves tracking off."""
    setup_site_time(proto)
    assert_goto_duration(proto, 6.0, -60.0, 6.0)
    assert proto.state()["tracking"] == "none"


@pytest.mark.parametrize("rate", [0, 1, 2])
def test_goto_preserves_tracking_rate(proto, rate):
    """A goto preserves both tracking state and the selected rate."""
    setup_site_time(proto)
    proto.control(tracking=MODES[rate])
    wait_tracking(proto, MODES[rate])
    assert_goto_duration(proto, 6.0, -60.0, 6.0)
    assert proto.state()["tracking"] == MODES[rate], \
        f"goto changed rate from {MODES[rate]} to {proto.state()['tracking']}"
