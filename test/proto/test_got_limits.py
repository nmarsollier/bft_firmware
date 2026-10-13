"""GOT-limits — behavior at and beyond the axis limits."""
import pytest

from client import DeviceError
from conftest import assert_goto_duration, setup_site_time


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_goto_beyond_dec_limit_rejected(proto):
    """DEC beyond the reachable range (dec_axis > ±150°) is rejected."""
    setup_site_time(proto)
    assert_goto_duration(proto, 6.0, -60.0, 6.0)
    with pytest.raises(DeviceError):
        proto.action("goto", ra=6.0, dec=80.0, speed=4)   # dec_axis ±170
    assert proto.state()["state"] != "slewing"


def test_goto_returns_to_valid_after_rejection(proto):
    """After a rejected out-of-limit goto the mount still slews to valid targets."""
    setup_site_time(proto)
    assert_goto_duration(proto, 6.0, -60.0, 6.0)
    with pytest.raises(DeviceError):
        proto.action("goto", ra=6.0, dec=80.0, speed=4)
    assert_goto_duration(proto, 6.0, -55.0, 6.0)
    assert proto.state()["state"] != "slewing"


def test_goto_to_exact_dec_limit(proto):
    """A goto to DEC=+60° lands at the DEC axis limit (±150°) and succeeds."""
    from conftest import assert_angular_close
    setup_site_time(proto)
    lst = proto.state()["lst"]
    m = assert_goto_duration(proto, lst, 60.0, 6.0)
    assert_angular_close(m["final_ra"], m["final_dec"], lst, 60.0, 0.1, "DEC=+60")
    assert abs(m["final_dec"] - 60.0) < 0.1, \
        f"DEC final {m['final_dec']} != +60 (limite)"
