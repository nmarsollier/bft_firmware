"""GOT-time — a goto's duration must match the slew ramp model."""
import pytest

from conftest import assert_goto_duration, setup_site_time


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_goto_duration_matches_ramp(proto):
    """A goto takes the time the acceleration ramp dictates."""
    setup_site_time(proto)
    assert_goto_duration(proto, 6.0, -60.0, 6.0)


def test_short_goto_duration(proto):
    """A short goto (triangular ramp) still matches the model."""
    setup_site_time(proto)
    assert_goto_duration(proto, 6.0, -65.0, 6.0)
