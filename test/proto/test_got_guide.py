"""GOT-guide — a PulseGuide issued mid-goto must not corrupt the slew."""
import pytest

from client import DeviceError
from conftest import setup_site_time, wait_not_moving, wait_slewing


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_pulse_guide_during_goto(proto):
    """A guide issued mid-slew is rejected (mount busy) without breaking the goto."""
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)

    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_slewing(proto)

    # the guide is rejected while the mount slews — the goto must still finish
    with pytest.raises(DeviceError):
        proto.action("guide", direction="north", duration_ms=200)

    wait_not_moving(proto)
    assert proto.state()["state"] != "slewing", "goto did not finish after guide"
    assert proto.state()["guiding"] is False, "left pulse-guiding active"
